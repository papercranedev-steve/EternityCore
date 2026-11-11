#include "Commander/Behaviors/Movement/OffRouteBehavior.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        Position3 PositionOf(Player const* me)
        {
            Position3 p;
            p.x = me->GetPositionX();
            p.y = me->GetPositionY();
            p.z = me->GetPositionZ();
            return p;
        }

        // The walk's end: ground of the field no patrol comes to before we leave (PatrolZone).
        bool Clear(WorldModel const& world, PositionField const& field, Position3 const& at, float staySec)
        {
            FieldCell const* c = field.At(at);
            return c && c->standable && !PatrolZone::Comes(world, at, staySec);
        }

        // Seconds left until the patrol of `t` comes, from now.
        float ArrivesInSec(WorldModel const& world, OffRouteTask const& t)
        {
            float const age = WorldTimer::getMSTimeDiff(world.refreshedAtMs, WorldTimer::getMSTime()) / 1000.0f;
            return t.arriveSec - age;
        }
    }

    bool OffRouteBehavior::AppliesTo(PartyBotAI const* /*bot*/) const
    {
        return true;
    }

    bool OffRouteBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& /*world*/, TeamPlan const& plan) const
    {
        return plan.routeGuard.Find(bot->me->GetObjectGuid()) != nullptr;
    }

    float OffRouteBehavior::CastBreakBenefit(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                             uint32 castLeftMs, char const*& reason) const
    {
        OffRouteTask const* t = plan.routeGuard.Find(bot->me->GetObjectGuid());
        if (!t)
            return 0.0f;
        float const leave = std::max(0.0f, t->leaveSec);
        if (ArrivesInSec(world, *t) > castLeftMs / 1000.0f + leave + CAST_MARGIN_SEC)
            return 0.0f;    // time to finish the cast first
        reason = "a patrol comes here before the cast ends and the walk out";
        return t->points;
    }

    void OffRouteBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        OffRouteTask const* t = plan.routeGuard.Find(me->GetObjectGuid());
        if (!t || !world.field || plan.retreat.active || me->HasUnitState(UNIT_STATE_ROOT) || me->GetTransport())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        PositionField const& field = *world.field;
        float const staySec = plan.routeGuard.staySec;

        // Walking off already (the walk's end is clear): keep walking.
        if (bot->IsMovementHeld() && me->IsMoving() && Clear(world, field, self->standAt, staySec))
        {
            Proposal hold;
            hold.tier = ProposalTier::Urgent;
            hold.score = 0.0f;
            hold.source = GetId();
            hold.reason = "walking off a patrol's route, keep walking";
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }

        Position3 const here = PositionOf(me);
        FieldSpots::Query q;
        q.center = here;
        q.minYd = 0.0f;
        q.maxYd = RouteGuard::LEAVE_SEARCH_YD;
        q.patrolBeforeSec = staySec;
        std::vector<FieldSpots::Spot> spots = FieldSpots::Collect(world, *self, q);
        auto const walk = [](FieldSpots::Spot const& s) { return s.WalkEstimate(PATH_FACTOR); };
        std::sort(spots.begin(), spots.end(), [&walk](FieldSpots::Spot const& a, FieldSpots::Spot const& b)
        {
            return walk(a) < walk(b);
        });
        FieldSpots::PreferFree(spots, walk, [](FieldSpots::Spot const& s) { return s.taken; });

        SafeRoute::IdleCreatures const idle(me);
        // The walk out of an idle creature's reach starts inside it.
        SafeRoute::IdleCreatures const pathIdle = idle.Near(here) ? idle.LeavingFrom(here) : idle;
        uint32 checks = 0;
        for (FieldSpots::Spot const& s : spots)
        {
            if (checks >= MAX_GEOMETRY_CHECKS)
                break;
            // This bot's own idle scan at the end (the field uses the party's farthest reach).
            if (idle.Near(s.pos))
                continue;
            ++checks;
            if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MAX_PATH_DETOUR))
                continue;

            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            float const walkSec = walk(s) / speed;
            Proposal p;
            p.tier = ProposalTier::Urgent;      // the brain's task: a must (header)
            p.score = -walkSec;
            p.source = GetId();
            p.reason = "a patrol comes here before we leave (TeamPlan::routeGuard)";
            p.action.type = BotActionType::MoveTo;
            p.action.x = s.pos.x;
            p.action.y = s.pos.y;
            p.action.z = s.pos.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + HOLD_EXTRA_MS;
            out.push_back(p);
            return;
        }
    }
}
