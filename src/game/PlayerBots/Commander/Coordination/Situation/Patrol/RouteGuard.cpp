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
        // The creatures of the pack the team hunts (HuntPlan::target): the fight we walk to, a
        // patrol among them is not one to step off from.
        std::vector<ObjectGuid> HuntedPack(WorldModel const& world, TeamPlan const& plan)
        {
            std::vector<ObjectGuid> out;
            if (!plan.hunt.Active())
                return out;
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == plan.hunt.target)
                    {
                        for (PackCreature const& m : pack.members)
                            out.push_back(m.guid);
                        return out;
                    }
            out.push_back(plan.hunt.target);
            return out;
        }

        // A patrol, not one of `ignore`, comes to `at` before `staySec` (PatrolZone): which
        // one and when (the first).
        PatrolFacts const* OnRoute(WorldModel const& world, Position3 const& at, float staySec,
                                   std::vector<ObjectGuid> const& ignore, float& arriveSec)
        {
            PatrolFacts const* first = nullptr;
            arriveSec = -1.0f;
            for (PatrolFacts const& p : world.patrols)
            {
                if (std::find(ignore.begin(), ignore.end(), p.guid) != ignore.end())
                    continue;
                float const sec = PatrolZone::ArriveSec(world, p, at);
                if (sec >= 0.0f && (!first || sec < arriveSec))
                {
                    first = &p;
                    arriveSec = sec;
                }
            }
            return first && arriveSec < staySec ? first : nullptr;
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
            if (plan.fightSpot.Moves(m.guid))
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

        // The patrol the team hunts comes to us on purpose: walking off its route would undo
        // the hunt's walk to it, every refresh (a field bot hunting a Prowler went back and
        // forth beside its route, 2026-11-22).
        std::vector<ObjectGuid> const hunted = HuntedPack(world, plan);

        // A whole fight on the route is PatrolAssigner's (header): the main tank stands there or
        // walks there (to an enemy of the fight on the route: anastari-20261127-224529, the tank
        // went back and forth for 10 s between the chase to a Gargoyle and stepping off the route).
        if (world.HasPartyEnemy())
        {
            float tankArrive = 0.0f;
            if (PartyMemberFacts const* tank = Tanks::Main(world))
                if (OnRoute(world, tank->pos, g.staySec, hunted, tankArrive) ||
                    OnRoute(world, tank->standAt, g.staySec, hunted, tankArrive))
                    return;
        }

        float const dealt = std::max(1.0f, Value::DealtDps(world));
        for (PartyMemberFacts const& m : world.party)
        {
            if (Exempt(plan, m))
                continue;
            float arrive = 0.0f;
            PatrolFacts const* p = OnRoute(world, m.standAt, g.staySec, hunted, arrive);
            // Walking off already: keep the task until it is off (OffRouteBehavior holds the walk).
            if (!p)
                p = OnRoute(world, m.pos, g.staySec, hunted, arrive);
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
