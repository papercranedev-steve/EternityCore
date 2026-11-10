#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // A patrol comes to the cell of `at` before `staySec`; its arrival in `arriveSec`.
        bool OnRoute(PositionField const& field, Position3 const& at, float staySec, float& arriveSec)
        {
            FieldCell const* c = field.At(at);
            if (!c || c->patrolSec < 0.0f || c->patrolSec >= staySec)
                return false;
            arriveSec = c->patrolSec;
            return true;
        }

        // The first patrol whose walk comes within its zone of `at`, and when (-1: none).
        PatrolFacts const* FirstPatrol(WorldModel const& world, Position3 const& at, float& sec)
        {
            PatrolFacts const* best = nullptr;
            sec = -1.0f;
            for (PatrolFacts const& p : world.patrols)
            {
                if (!p.aggressive || p.noAggro || p.tappedByOther || p.track.empty())
                    continue;
                float const radius = p.reach + PositionField::PATROL_MARGIN;
                for (size_t i = 0; i < p.track.size(); ++i)
                {
                    float const t = float(i) * p.trackStepSec;
                    if (best && t >= sec)
                        break;
                    if (PackResolver::Distance(p.track[i], at) <= radius)
                    {
                        best = &p;
                        sec = t;
                        break;
                    }
                }
            }
            return best;
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
        PositionField const& field = *world.field;

        // A whole fight on the route is PatrolAssigner's (header).
        if (world.HasPartyEnemy())
        {
            float tankArrive = 0.0f;
            if (PartyMemberFacts const* tank = Tanks::Main(world))
                if (OnRoute(field, tank->pos, g.staySec, tankArrive))
                    return;
        }

        float const dealt = std::max(1.0f, Value::DealtDps(world));
        for (PartyMemberFacts const& m : world.party)
        {
            if (Exempt(plan, m))
                continue;
            float arrive = 0.0f;
            Position3 at = m.standAt;
            if (!OnRoute(field, at, g.staySec, arrive))
            {
                // Walking off already: keep the task until it is off (OffRouteBehavior holds the walk).
                at = m.pos;
                if (!OnRoute(field, at, g.staySec, arrive))
                    continue;
            }

            OffRouteTask t;
            t.bot = m.guid;
            t.arriveSec = arrive;
            float sec = -1.0f;
            if (PatrolFacts const* p = FirstPatrol(world, at, sec))
            {
                t.patrol = p->guid;
                float const killSec = float(p->health) / dealt;
                t.points = Value::Points(world, p->meleeDps * std::max(0.0f, g.staySec - arrive)) +
                           Value::FightExtension(world, killSec);
            }

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
