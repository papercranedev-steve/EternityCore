#include "Commander/Behaviors/HealPlanBehavior.h"
#include "Commander/Behaviors/HealPlan.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "HealSpells.h"
#include "Map.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        struct Candidate
        {
            Unit* unit = nullptr;
            PartyMemberFacts const* facts = nullptr;
            HealPlan::Target target;
            HealPlan::Assessment assessment;
            std::vector<HealPlan::Option> options;
            std::vector<SpellEntry const*> spells;      // same index as options
        };

        bool IsMoreUrgent(Candidate const& a, Candidate const& b)
        {
            bool const ea = a.assessment.need == HealPlan::Need::Emergency;
            bool const eb = b.assessment.need == HealPlan::Need::Emergency;
            if (ea != eb)
                return ea;
            if (ea)
                return a.assessment.timeToDie < b.assessment.timeToDie;
            if (a.facts->isPet != b.facts->isPet)
                return !a.facts->isPet;
            // A chain and a lift by who reaches the reserve first: until 2026-11-02 a chain came
            // before every lift, and the priest topped up a rogue at 65% while the tank sank from
            // 53% into an emergency (strat-20261102-012937). The chain only breaks ties.
            if (a.assessment.timeToReserve != b.assessment.timeToReserve)
                return a.assessment.timeToReserve < b.assessment.timeToReserve;
            bool const ca = a.assessment.need == HealPlan::Need::Chain;
            bool const cb = b.assessment.need == HealPlan::Need::Chain;
            if (ca != cb)
                return ca;
            return a.facts->healthPct < b.facts->healthPct;
        }

        // Power Word: Shield instead of the picked heal (see the header): an emergency the heal
        // does not reach in time, or the healer itself being hit below SHIELD_SELF_PCT before a
        // heal with a cast time (no pushback under the shield). A heal order like the heals.
        bool ProposeShield(PartyBotAI const* bot, WorldModel const& world,
                           Unit* unit, PartyMemberFacts const& facts, HealPlan::Target const& target,
                           HealPlan::Assessment const& assessment, HealPlan::Option const* heal,
                           std::vector<Proposal>& out)
        {
            SpellEntry const* shield = bot->Kit().AllyShield();
            if (!shield)
                return false;

            bool const emergency = assessment.need == HealPlan::Need::Emergency;
            float const landSec = heal ? heal->castSec + HealPlan::LANDING_MARGIN_SEC : HealPlan::NO_LIMIT;
            bool const tooLate = emergency && assessment.timeToDie < landSec;
            bool const self = unit == bot->me;
            bool const pushback = self && target.intake > 0.0f && heal && heal->castSec > 0.0f &&
                                  unit->GetHealthPercent() < HealPlanBehavior::SHIELD_SELF_PCT;
            if (!tooLate && !pushback)
                return false;
            // Weakened Soul gives shield immunity; one shield at a time.
            if (unit->IsImmuneToMechanic(MECHANIC_SHIELD) || unit->HasAura(shield->Id) ||
                !bot->CanTryToCastSpell(unit, shield))
                return false;

            float const absorb = float(std::max(0, shield->CalculateSimpleValue(EFFECT_INDEX_0)));
            float const durationSec = std::max(0, shield->GetDuration()) / 1000.0f;
            float const taken = std::min(absorb, target.intake * durationSec);
            // Benefit: the hits it takes; plus the death when the heal would land too late and
            // the shield holds until the next heal can (one global cooldown, then that heal).
            float benefit = Value::Points(world, taken);
            if (tooLate && target.intake > 0.0f &&
                assessment.timeToDie + absorb / target.intake >= HealPlan::GCD_SEC + (heal ? heal->castSec : 0.0f))
                benefit += Value::DeathPenalty(facts);

            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = tooLate ? "heal lands too late: shield" : "hit while casting: shield";
            p.action.type = BotActionType::Cast;
            p.action.target = facts.guid;
            p.action.spell = shield;
            // Cost: absorb left over when it runs out (like overheal).
            p.SetValue(benefit, Value::Points(world, absorb - taken));
            out.push_back(p);
            return true;
        }

        char const* ReasonOf(HealPlan::Need need)
        {
            switch (need)
            {
                case HealPlan::Need::Emergency: return "emergency heal";
                case HealPlan::Need::Chain:     return "continue heal chain";
                default:                        return "deficit reached, lift";
            }
        }
    }

    bool HealPlanBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER;
    }

    bool HealPlanBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world) const
    {
        return what == Takeover::Healing && ControlsHealingOf(bot, world);
    }

    bool HealPlanBehavior::ControlsHealingOf(PartyBotAI const* bot, WorldModel const& world)
    {
        return sCommanderMgr.IsEnabled() && sCommanderMgr.IsModuleEnabled("heal") &&
               bot->GetRole() == ROLE_HEALER && bot->me->IsInCombat() && !bot->IsInDuel() &&
               world.HasPartyEnemy();
    }

    void HealPlanBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        if (!ControlsHealingOf(bot, world))
            return;

        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        if (!myFacts)
            return;

        uint32 const now = WorldTimer::getMSTime();
        uint32 const tickMs = sCommanderMgr.GetBotUpdateInterval();

        HealPlan::Healer healer;
        healer.regen = myFacts->manaRegen;
        healer.regenCasting = myFacts->manaRegenCasting;
        healer.tickSec = tickMs / 1000.0f;
        ManaBudgetBehavior::Budget budget;
        float const tension = ManaBudgetBehavior::ComputeBudget(bot, world, budget) ? budget.tension : 0.0f;
        float const manaPct = myFacts->maxMana ? float(myFacts->mana) * 100.0f / float(myFacts->maxMana) : 100.0f;
        healer.manaWeight = HealPlan::ManaWeight(tension, manaPct);

        BotDirectives const& directives = bot->Directives();
        std::vector<Candidate> candidates;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive || m.charmed)
                continue;
            Unit* unit = me->GetMap()->GetUnit(m.guid);
            if (!unit || !bot->IsValidHealTarget(unit))
                continue;

            // Castable single-target heals with a direct part (HoTs left out, see header).
            HealChoice choice;
            bot->ChooseHeal(unit, choice, true);
            if (choice.options.empty())
                continue;

            Candidate c;
            c.unit = unit;
            c.facts = &m;
            c.target.health = float(unit->GetHealth());
            c.target.maxHealth = float(unit->GetMaxHealth());
            c.target.intake = Value::TakenDps(world, m.guid);
            c.target.burst = m.takenBurst;
            c.target.chained = directives.IsHealChain(m.guid, now, tickMs + CHAIN_GAP_MS);
            for (HealSpells::Option const& o : choice.options)
            {
                if (o.amount.direct <= 0.0f)
                    continue;
                HealPlan::Option p;
                p.heal = o.amount.direct;
                if (o.amount.periodic > 0.0f)
                    p.heal += std::min(o.amount.periodic,
                                       c.target.intake * o.amount.periodicSec * HealSpells::HOT_INCOMING_SHARE);
                p.mana = o.mana;
                p.castSec = o.castSec;
                c.options.push_back(p);
                c.spells.push_back(o.spell);
            }
            c.assessment = HealPlan::Assess(c.target, healer, c.options);
            if (c.assessment.need == HealPlan::Need::None || c.assessment.need == HealPlan::Need::Rest)
                continue;
            candidates.push_back(std::move(c));
        }
        if (candidates.empty())
            return;     // rest: the rotation heals nobody this tick

        std::stable_sort(candidates.begin(), candidates.end(), IsMoreUrgent);

        bool const checkAggro = sCommanderMgr.IsModuleEnabled("threat");
        for (Candidate& c : candidates)
        {
            bool const emergency = c.assessment.need == HealPlan::Need::Emergency;
            // Emergencies prefer heals that do not pull, but take one that does (HealPlan::Pick).
            if (checkAggro && c.unit->GetHealthPercent() >= ThreatBehavior::HEAL_IGNORE_THREAT_PCT)
                for (size_t i = 0; i < c.options.size(); ++i)
                    c.options[i].pulls = ThreatBehavior::HealPullLoss(bot, world, c.unit, c.spells[i]) > 0.0f;

            int const index = HealPlan::Pick(c.target, healer, c.assessment, c.options);
            if (ProposeShield(bot, world, c.unit, *c.facts, c.target, c.assessment,
                              index >= 0 ? &c.options[index] : nullptr, out))
                return;
            if (index < 0)
                continue;

            HealPlan::Option const& o = c.options[index];
            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = ReasonOf(c.assessment.need);
            p.action.type = BotActionType::Cast;
            p.action.target = c.facts->guid;
            p.action.spell = c.spells[index];
            // Benefit: the health it restores, plus the death it prevents when the target would
            // not last until the next chance to heal it. Cost: the overheal.
            float benefit = Value::Points(world, o.effective);
            if (emergency && c.assessment.timeToDie < std::max(HealPlan::GCD_SEC, o.castSec) + healer.tickSec * 2.0f)
                benefit += Value::DeathPenalty(*c.facts);
            p.SetValue(benefit, Value::Points(world, o.heal - o.effective));
            out.push_back(p);
            return;
        }
    }
}
