#include "Commander/Behaviors/Support/RestBehavior.h"
#include "Commander/Behaviors/Movement/HealerStanceBehavior.h"
#include "Commander/Behaviors/Movement/RangedStanceBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/RestValue.h"
#include "Commander/Perception/Tanks.h"
#include "PartyBotAI.h"
#include "Map.h"

namespace Commander
{
    namespace
    {
        bool InReach(PartyBotAI const* bot, Position3 const& from, Position3 const& to, float range)
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
        bool PlaceOk(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                     PartyMemberFacts const& self)
        {
            Position3 planned;
            bool const beforeFight = !world.HasPartyEnemy() && plan.pull.PlannedFightSpot(planned);
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

    bool RestBehavior::AppliesTo(PartyBotAI const* /*bot*/) const
    {
        return true;
    }

    void RestBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        // Retreat, the boss module's waves, the pull's tank on its way (the plan's constraint).
        if (plan.ForbidsRest(me->GetObjectGuid()))
            return;
        if (me->IsInCombat() || me->GetVictim() || me->IsNonMeleeSpellCasted(false, false, true))
            return;
        // Walking to its place (stance modules, the pull's hiding point): arrive first.
        if (bot->IsMovementHeld() && me->IsMoving())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        RestValue::Worth const w = RestValue::Of(world, *self, plan.pull.PlannedFight());
        if (!w.Pays())
            return;
        if (!w.free && !PlaceOk(bot, world, plan, *self))
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
        out.push_back(p);
    }
}
