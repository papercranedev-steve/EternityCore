#include "Commander/Behaviors/Support/RestBehavior.h"
#include "Commander/Behaviors/Movement/HealerStanceBehavior.h"
#include "Commander/Behaviors/Movement/RangedStanceBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Perception/Values/RestValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "HealSpells.h"
#include <algorithm>
#include <vector>

namespace Commander
{
    namespace
    {
        bool InReach(TeamBotAI const* bot, Position3 const& from, Position3 const& to, float range)
        {
            if (SafeRoute::Distance2d(from, to) > range)
                return false;
            float const eye = RestBehavior::LOS_EYE_HEIGHT;
            return bot->me->GetMap()->isInLineOfSight(from.x, from.y, from.z + eye, to.x, to.y, to.z + eye);
        }

        // Where a member rests while a fight is on or planned: a healer where it can heal from
        // (the main tank, or before the fight the planned fight spot), a ranged damage dealer
        // before the fight in cast range of the planned fight spot. Elsewhere the stance
        // modules walk it there first.
        bool PlaceOk(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                     PartyMemberFacts const& self)
        {
            Position3 planned;
            bool const beforeFight = !world.HasPartyEnemy() && plan.PlannedFightSpot(planned);
            if (bot->GetRole() == ROLE_HEALER)
            {
                if (beforeFight)
                    return InReach(bot, self.pos, planned, HealerStanceBehavior::HEAL_RANGE);
                PartyMemberFacts const* tank = Tanks::Main(world);
                return !tank || tank->guid == self.guid ||
                       InReach(bot, self.pos, tank->pos, HealerStanceBehavior::HEAL_RANGE);
            }
            if (bot->GetRole() == ROLE_RANGE_DPS && bot->UsesCasterChase() && beforeFight)
                return InReach(bot, self.pos, planned, RangedStanceBehavior::CAST_RANGE);
            return true;
        }
    }

    bool RestBehavior::AppliesTo(TeamBotAI const* /*bot*/) const
    {
        return true;
    }

    void RestBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        // Retreat, the boss module's waves, the pull's tank on its way (the plan's constraint).
        // The next wave too close for a new drink (noNewRest): one already on goes on.
        if (bot->IsResting() ? plan.ForbidsRest(me->GetObjectGuid()) : plan.ForbidsNewRest(me->GetObjectGuid()))
            return;
        // The leader walked away: it catches up first (Movement/RestCatchUp.h).
        if (bot->HoldsOffRest())
            return;
        if (me->IsInCombat() || me->GetVictim() || me->IsNonMeleeSpellCasted(false, false, true))
            return;
        // Walking to its place (stance modules, the pull's hiding point): arrive first.
        if (bot->IsMovementHeld() && me->IsMoving())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        RestValue::Worth const w = RestValue::Of(world, *self, plan.pull.PlannedFight(),
                                                 PullPlanner::ReadyManaShare(plan.pull, *self));
        if (!w.Pays())
            return;
        if (!w.free && !PlaceOk(bot, world, plan, *self))
            return;
        // In bear or cat form only eating counts (no drink there); StartResting leaves the form
        // for it when it may (TeamBotAI::MayLeaveFormOutOfCombat), else no rest is proposed.
        bool const shifted = me->IsShapeShifted() && me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT);
        if (shifted && (!w.eat || !bot->MayLeaveFormOutOfCombat(0, self->hasDrink)))
            return;

        Proposal p;
        p.source = "rest";
        p.action.type = BotActionType::Rest;
        if (w.free)
        {
            p.tier = ProposalTier::Routine;
            p.reason = "rest, nothing else to do";
        }
        else
        {
            p.tier = ProposalTier::Assigned;
            p.reason = bot->IsResting() ? "keep resting, mana for this fight" : "rest, mana for this fight";
            p.SetValue(w.benefit, w.cost);
        }
        if (shifted)
        {
            p.reason = "rest: out of the form to eat";
            out.push_back(p);
            return;
        }

        // Health to mana first while that shortens the rest by more than the cast's global
        // cooldown (RestValue::TapSavesSec): the same place in the arbitration as the rest it
        // shortens, one tap per tick, then it sits down.
        if (SpellEntry const* tap = bot->Kit().HealthToMana())
        {
            if (RestValue::TapSavesSec(world, *self) > Value::GCD_SEC)
            {
                if (bot->CanTryToCastSpell(me, tap))
                {
                    p.action.type = BotActionType::Cast;
                    p.action.spell = tap;
                    p.action.target = self->guid;
                    p.reason = "rest: health to mana first, eat and drink together";
                }
                // The last tap's global cooldown: wait it out standing. Sitting down now
                // started a drink the next tap stood up from (user 2026-11-26: a tap, a drink,
                // three times over, two drinks thrown away).
                else if (me->HasGCD(tap))
                {
                    p.action.type = BotActionType::Hold;
                    p.reason = "rest: health to mana first, the next tap after the global cooldown";
                }
            }
        }

        // Mana to health first (user 2026-11-27, every class with a self-heal; the tap's
        // reverse): the heal that shortens the rest by the most beyond its busy time
        // (RestValue::SelfHealSavesSec), cast in the rest's place; its global cooldown waited
        // out standing, as the tap's.
        if (p.action.type == BotActionType::Rest)
        {
            std::vector<HealSpells::Option> heals;
            bot->RestSelfHeals(heals);
            // (its live health and mana: the snapshot may not have the last heal yet)
            PartyMemberFacts live = *self;
            if (!heals.empty())
            {
                live.health = me->GetHealth();
                live.mana = me->GetPower(POWER_MANA);
            }
            HealSpells::Option const* best = nullptr;
            float bestGain = 0.0f;
            for (HealSpells::Option const& o : heals)
            {
                float const busy = std::max(o.castSec, Value::GCD_SEC);
                float const gain = RestValue::SelfHealSavesSec(world, live, o.amount.direct + o.amount.periodic, o.mana) - busy;
                if (gain > bestGain)
                {
                    bestGain = gain;
                    best = &o;
                }
            }
            if (best && bot->CanTryToCastSpell(me, best->spell))
            {
                p.action.type = BotActionType::Cast;
                p.action.spell = best->spell;
                p.action.target = self->guid;
                p.reason = "rest: mana to health first, eat and drink together";
            }
            else if (best && me->HasGCD(best->spell))
            {
                p.action.type = BotActionType::Hold;
                p.reason = "rest: mana to health first, the next heal after the global cooldown";
            }
        }
        out.push_back(p);
    }
}
