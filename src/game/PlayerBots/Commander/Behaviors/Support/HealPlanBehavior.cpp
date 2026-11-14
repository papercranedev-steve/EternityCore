#include "Commander/Behaviors/Support/HealPlanBehavior.h"
#include "Commander/Behaviors/Support/HealPlan.h"
#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
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

            // Someone about to die: the team rescue's choice (RescueAssigner, Takeover::Survival).
            bool const emergency = assessment.need == HealPlan::Need::Emergency;
            bool const self = unit == bot->me;
            bool const pushback = self && target.intake > 0.0f && heal && heal->castSec > 0.0f &&
                                  unit->GetHealthPercent() < HealPlanBehavior::SHIELD_SELF_PCT;
            if (!pushback)
                return false;
            // Weakened Soul gives shield immunity; one shield at a time.
            if (unit->IsImmuneToMechanic(MECHANIC_SHIELD) || unit->HasAura(shield->Id) ||
                !bot->CanTryToCastSpell(unit, shield))
                return false;

            float const absorb = float(std::max(0, shield->CalculateSimpleValue(EFFECT_INDEX_0)));
            float const durationSec = std::max(0, shield->GetDuration()) / 1000.0f;
            float const taken = std::min(absorb, target.intake * durationSec);
            // Benefit: the hits it takes.
            float const benefit = Value::Points(world, taken);

            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = "hit while casting: shield";
            p.action.type = BotActionType::Cast;
            p.action.target = facts.guid;
            p.action.spell = shield;
            // Cost: absorb left over when it runs out (like overheal).
            p.SetValue(benefit, Value::Points(world, absorb - taken));
            out.push_back(p);
            return true;
        }

        // The free self heal (Desperate Prayer, see the header): hit, and missing at least about
        // its amount. Not while another member needs an emergency heal (it takes a GCD).
        bool ProposeFreeSelfHeal(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                                 bool otherEmergency, std::vector<Proposal>& out)
        {
            SpellEntry const* spell = bot->Kit().FreeSelfHeal();
            if (!spell || otherEmergency || !self.CanAct())
                return false;
            Player* me = bot->me;
            if (Value::TakenDps(world, self.guid) <= 0.0f)
                return false;
            if (!me->IsSpellReady(spell) || !bot->CanTryToCastSpell(me, spell))
                return false;
            float const heal = HealSpells::Estimate(me, me, spell).direct;
            float const missing = float(me->GetMaxHealth()) - float(me->GetHealth());
            if (heal <= 0.0f || missing < heal * (1.0f - HealPlan::OVERHEAL_MAX))
                return false;

            Proposal p;
            p.tier = SurvivalValue::Of(world, self).InDanger() ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = "hit, free self heal fits";
            p.action.type = BotActionType::Cast;
            p.action.target = self.guid;
            p.action.spell = spell;
            float const effective = std::min(heal, missing);
            p.SetValue(Value::Points(world, effective), Value::Points(world, heal - effective));
            out.push_back(p);
            return true;
        }

        // A free heal boost is ready (Inner Focus): the deficit the lift waits for, so the boosted
        // biggest heal fills it (HealPlan::Target::waitDeficit). 0 = none ready.
        float FreeBoostWait(PartyBotAI const* bot, std::vector<HealPlan::Option> const& options)
        {
            Player* me = bot->me;
            for (HealBoostSpell const& s : bot->Kit().HealBoosts())
            {
                if (!s.spell || !s.free)
                    continue;
                if (!me->HasAura(s.spell->Id) && (!me->IsSpellReady(s.spell) || !bot->CanTryToCastSpell(me, s.spell)))
                    continue;
                HealPlan::Boost b;
                b.free = true;
                b.critChance = s.critChance;
                float biggest = 0.0f;
                for (HealPlan::Option const& o : options)
                    biggest = std::max(biggest, HealPlan::BoostedHeal(o, b));
                return biggest * (1.0f - HealPlan::OVERHEAL_MAX);
            }
            return 0.0f;
        }

        // A ready heal boost that suits this heal (HealPlan::PickBoosted): the option to cast
        // with it, and the boost; -1 = none.
        int PickBoost(PartyBotAI const* bot, Candidate const& c, int picked,
                      SpellEntry const*& boostSpell, HealPlan::Boost& boost)
        {
            Player* me = bot->me;
            for (HealBoostSpell const& s : bot->Kit().HealBoosts())
            {
                if (!s.spell)
                    continue;
                // Already on (put up in a tick whose heal did not go out): counts as ready.
                if (!me->HasAura(s.spell->Id) && (!me->IsSpellReady(s.spell) || !bot->CanTryToCastSpell(me, s.spell)))
                    continue;
                HealPlan::Boost b;
                b.free = s.free;
                b.critChance = s.critChance;
                b.instant = s.instant;
                int const index = HealPlan::PickBoosted(c.target, c.assessment, c.options, picked, b);
                if (index < 0)
                    continue;
                boostSpell = s.spell;
                boost = b;
                return index;
            }
            return -1;
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
        return bot->GetRole() == ROLE_HEALER || bot->Kit().FreeSelfHeal();
    }

    bool HealPlanBehavior::ControlsSelfHealOf(PartyBotAI const* bot, WorldModel const& world)
    {
        return sCommanderMgr.IsEnabled() && sCommanderMgr.IsModuleEnabled("heal") &&
               world.HasPartyEnemy();
    }

    bool HealPlanBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Healing && ControlsHealingOf(bot, world);
    }

    bool HealPlanBehavior::ControlsHealingOf(PartyBotAI const* bot, WorldModel const& world)
    {
        return sCommanderMgr.IsEnabled() && sCommanderMgr.IsModuleEnabled("heal") &&
               bot->GetRole() == ROLE_HEALER && world.HasPartyEnemy();
    }

    void HealPlanBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        if (!myFacts)
            return;
        if (!ControlsHealingOf(bot, world))
        {
            // A bot that is not a healer: only its free self heal (user 2026-11-14).
            if (ControlsSelfHealOf(bot, world))
                ProposeFreeSelfHeal(bot, world, *myFacts, false, out);
            return;
        }

        // Still out of combat and held by the pull's opening (PullOpening.h: healing starts last,
        // its threat lands on every enemy at once): emergencies only.
        bool const heldByPull = !me->IsInCombat() && plan.pull.HoldsWaiting(true);
        SpellEntry const* freeSelfHeal = bot->Kit().FreeSelfHeal();

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
                // The free self heal has its own rule (ProposeFreeSelfHeal).
                if (o.amount.direct <= 0.0f || o.spell == freeSelfHeal)
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
            c.target.waitDeficit = FreeBoostWait(bot, c.options);
            c.assessment = HealPlan::Assess(c.target, healer, c.options);
            if (c.assessment.need == HealPlan::Need::None || c.assessment.need == HealPlan::Need::Rest)
                continue;
            candidates.push_back(std::move(c));
        }
        if (candidates.empty())
            return;     // rest: the rotation heals nobody this tick

        std::stable_sort(candidates.begin(), candidates.end(), IsMoreUrgent);

        // The free self heal first, unless someone else needs an emergency heal (user 2026-11-14).
        bool const otherEmergency = std::any_of(candidates.begin(), candidates.end(), [me](Candidate const& c)
            { return c.unit != me && c.assessment.need == HealPlan::Need::Emergency; });
        if (!heldByPull && ProposeFreeSelfHeal(bot, world, *myFacts, otherEmergency, out))
            return;

        bool const checkAggro = sCommanderMgr.IsModuleEnabled("threat");
        for (Candidate& c : candidates)
        {
            bool const emergency = c.assessment.need == HealPlan::Need::Emergency;
            if (heldByPull && !emergency)
                continue;
            // Emergencies prefer heals that do not pull, but take one that does (HealPlan::Pick).
            if (checkAggro && c.unit->GetHealthPercent() >= ThreatBehavior::HEAL_IGNORE_THREAT_PCT)
                for (size_t i = 0; i < c.options.size(); ++i)
                    c.options[i].pulls = ThreatBehavior::HealPullLoss(bot, world, c.unit, c.spells[i]) > 0.0f;

            int index = HealPlan::Pick(c.target, healer, c.assessment, c.options);
            if (ProposeShield(bot, world, c.unit, *c.facts, c.target, c.assessment,
                              index >= 0 ? &c.options[index] : nullptr, out))
                return;

            // A ready heal boost that suits this heal: cast with the heal it waits for.
            SpellEntry const* boostSpell = nullptr;
            HealPlan::Boost boost;
            int const boosted = PickBoost(bot, c, index, boostSpell, boost);
            if (boosted >= 0)
                index = boosted;
            if (index < 0)
                continue;

            HealPlan::Option const& o = c.options[index];
            float heal = o.heal;
            float effective = o.effective;
            float castSec = o.castSec;
            if (boostSpell)
            {
                castSec = boost.instant ? 0.0f : o.castSec;
                heal = HealPlan::BoostedHeal(o, boost);
                float const missing = std::max(0.0f, c.target.maxHealth - c.target.health);
                effective = std::min(heal, std::min(c.target.maxHealth, missing + c.target.intake * castSec));
            }
            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = boostSpell ? (boost.instant ? "emergency: boost, instant big heal" : "big deficit: boost, big heal")
                                  : ReasonOf(c.assessment.need);
            p.action.type = BotActionType::Cast;
            p.action.target = c.facts->guid;
            p.action.spell = c.spells[index];
            p.action.prepSpell = boostSpell;
            // Benefit: the health it restores, plus the death it prevents when the target would
            // not last until the next chance to heal it, plus the mana a free boost saves.
            // Cost: the overheal.
            float benefit = Value::Points(world, effective);
            if (emergency && c.assessment.timeToDie < std::max(HealPlan::GCD_SEC, castSec) + healer.tickSec * 2.0f)
                benefit += Value::DeathPenalty(*c.facts);
            if (boostSpell && boost.free)
                benefit += ManaWorth::Points(world, *myFacts, o.mana);
            p.SetValue(benefit, Value::Points(world, heal - effective));
            out.push_back(p);
            return;
        }
    }
}
