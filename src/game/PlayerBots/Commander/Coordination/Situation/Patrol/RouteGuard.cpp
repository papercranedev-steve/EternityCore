#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // A patrol comes to `at` before `staySec` (PatrolZone): which one and when.
        PatrolFacts const* OnRoute(WorldModel const& world, Position3 const& at, float staySec, float& arriveSec)
        {
            PatrolFacts const* p = nullptr;
            arriveSec = PatrolZone::ArriveSec(world, at, &p);
            return arriveSec >= 0.0f && arriveSec < staySec ? p : nullptr;
        }

        bool Exempt(TeamPlan const& plan, PartyMemberFacts const& m)
        {
            if (!m.isBot || m.isPet || !m.alive)
                return true;
            PullPlan const& pull = plan.pull;
            if (m.guid == pull.tank && (pull.phase == PullPhase::Approach || pull.phase == PullPhase::Return))
                return true;
            // (its spot was checked against patrols for the time it stands there: PullControl)
            if (pull.control.Active() && pull.control.bot == m.guid)
                return true;
            // The patrol hold's controller (PatrolHold.h): its spot is off the patrol's walk until
            // it is held there; afterwards the patrol stands still.
            if (plan.patrolHold.active && plan.patrolHold.bot == m.guid)
                return true;
            if (pull.LosHiderOf(m.guid))
                return true;
            PatrolMovePlan const& move = plan.patrolMove;
            if (move.active && move.phase == PatrolPhase::Move &&
                (move.tank == m.guid || std::any_of(move.followers.begin(), move.followers.end(),
                    [&m](PatrolFollower const& f) { return f.guid == m.guid; })))
                return true;
            return false;
        }
    }

    namespace RouteGuard
    {
        float StaySec(WorldModel const& world, TeamPlan const& plan)
        {
            if (world.HasPartyEnemy())
                return Value::FightRemainingSec(world);
            if (plan.pull.phase != PullPhase::None)
            {
                float const fight = plan.pull.expectedFightSec > 0.0f ? plan.pull.expectedFightSec : PULL_FALLBACK_FIGHT_SEC;
                return fight + PullPlanner::PULL_LEAD_SEC;
            }
            return TRAVEL_STAY_SEC;
        }
    }

    void RouteGuardAssigner::Clear(TeamPlan& plan) const
    {
        plan.routeGuard = RouteGuardPlan();
    }

    void RouteGuardAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        RouteGuardPlan& g = plan.routeGuard;
        g.tasks.clear();
        g.staySec = RouteGuard::StaySec(world, plan);
        if (!world.field || plan.retreat.active || g.staySec <= 0.0f)
            return;

        // A whole fight on the route is PatrolAssigner's (header).
        if (world.HasPartyEnemy())
        {
            float tankArrive = 0.0f;
            if (PartyMemberFacts const* tank = Tanks::Main(world))
                if (OnRoute(world, tank->pos, g.staySec, tankArrive))
                    return;
        }

        float const dealt = std::max(1.0f, Value::DealtDps(world));
        for (PartyMemberFacts const& m : world.party)
        {
            if (Exempt(plan, m))
                continue;
            float arrive = 0.0f;
            PatrolFacts const* p = OnRoute(world, m.standAt, g.staySec, arrive);
            // Walking off already: keep the task until it is off (OffRouteBehavior holds the walk).
            if (!p)
                p = OnRoute(world, m.pos, g.staySec, arrive);
            if (!p)
                continue;

            OffRouteTask t;
            t.bot = m.guid;
            t.arriveSec = arrive;
            t.patrol = p->guid;
            float const killSec = float(p->health) / dealt;
            t.points = Value::Points(world, p->meleeDps * std::max(0.0f, g.staySec - arrive)) +
                       Value::FightExtension(world, killSec);

            FieldSpots::Query q;
            q.center = m.pos;
            q.minYd = 0.0f;
            q.maxYd = RouteGuard::LEAVE_SEARCH_YD;
            q.patrolBeforeSec = g.staySec;
            float best = -1.0f;
            for (FieldSpots::Spot const& s : FieldSpots::Collect(world, m, q))
            {
                float const walk = s.WalkEstimate(RouteGuard::PATH_FACTOR);
                if (best < 0.0f || walk < best)
                    best = walk;
            }
            t.leaveSec = best < 0.0f ? -1.0f : best / RouteGuard::RUN_YARDS_PER_SEC;
            g.tasks.push_back(t);
        }
    }
}
