#include "Commander/Behaviors/HealPlanBehavior.h"
#include "Commander/Behaviors/HealPlan.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/BotDirectives.h"
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

        int OrderGroup(HealPlan::Need need)
        {
            switch (need)
            {
                case HealPlan::Need::Emergency: return 0;
                case HealPlan::Need::Chain:     return 1;
                default:                        return 2;
            }
        }

        bool IsMoreUrgent(Candidate const& a, Candidate const& b)
        {
            int const ga = OrderGroup(a.assessment.need);
            int const gb = OrderGroup(b.assessment.need);
            if (ga != gb)
                return ga < gb;
            if (ga == 0)
                return a.assessment.timeToDie < b.assessment.timeToDie;
            if (a.facts->isPet != b.facts->isPet)
                return !a.facts->isPet;
            if (a.assessment.timeToReserve != b.assessment.timeToReserve)
                return a.assessment.timeToReserve < b.assessment.timeToReserve;
            return a.facts->healthPct < b.facts->healthPct;
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

    bool HealPlanBehavior::ControlsHealing(PartyBotAI const* bot, WorldModel const& world) const
    {
        return ControlsHealingOf(bot, world);
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
