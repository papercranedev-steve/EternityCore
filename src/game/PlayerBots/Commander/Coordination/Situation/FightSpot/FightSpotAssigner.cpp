#include "Commander/Coordination/Situation/FightSpot/FightSpotAssigner.h"
#include "Commander/Coordination/Situation/FightSpot/FightSpot.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Tanks.h"
#include "Timer.h"
#include <cfloat>
#include <cstdarg>
#include <cstdio>

namespace Commander
{
    namespace
    {
        std::string NameOf(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile ? profile->name : std::string("patrol");
        }

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        PatrolFacts const* FindPatrol(WorldModel const& world, ObjectGuid guid)
        {
            for (PatrolFacts const& p : world.patrols)
                if (p.guid == guid)
                    return &p;
            return nullptr;
        }

        // Why the place loses points, for the party chat.
        std::string Reason(FightSpot::Choice const& c)
        {
            std::string const name = c.patrol ? NameOf(c.patrol->entry) : std::string();
            std::string out;
            auto const add = [&out](std::string const& part) { out += (out.empty() ? "" : ", ") + part; };
            if (c.herePatrols > 0.0f)
                add(name + " walks into the fight");
            if (c.hereHolds > 0.0f)
                add("a control that breaks on damage is next to the pile");
            if (c.hereFlee > 0.0f)
                add("a runner would bring more from here");
            if (c.bestArea < 0.0f)
                add("the area caster has more room there");
            return out.empty() ? std::string("a better spot") : out;
        }

        void End(FightSpotPlan& m)
        {
            m.moving = false;
            m.spots.clear();
            m.chosen = -1;
        }
    }

    bool FightSpotAssigner::HoldsMost(WorldModel const& world, ObjectGuid tank)
    {
        uint32 total = 0;
        uint32 onTank = 0;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || !e.health || e.crowdControlled || e.onAllyOnly || !world.FindMember(e.victim))
                continue;
            ++total;
            if (e.victim == tank)
                ++onTank;
        }
        return onTank * 2 >= total;
    }

    void FightSpotAssigner::Clear(TeamPlan& plan) const
    {
        plan.fightSpot = FightSpotPlan();
    }

    void FightSpotAssigner::Continue(WorldModel const& world, TeamPlan& plan, PartyMemberFacts const& tank) const
    {
        FightSpotPlan& m = plan.fightSpot;
        // (a move for a patrol that joined the fight or left the scan goes on: the spot is
        // judged again from where the tank arrives)
        PatrolFacts const* patrol = m.patrol.IsEmpty() ? nullptr : FindPatrol(world, m.patrol);
        std::string const name = patrol ? NameOf(patrol->entry) : std::string("The fight");
        if (m.tank != tank.guid)
        {
            End(m);
            return;
        }
        // The enemies the tank does not hold stay with their victims while it walks
        // (strat-20261108-200046: 15 s walking with one of six enemies on it).
        if (!HoldsMost(world, tank.guid))
        {
            plan.notes.push_back(name + ": the tank lost most enemies while moving; taking them back first.");
            End(m);
            return;
        }
        if (m.chosen == -2)
        {
            plan.notes.push_back("No reachable spot for the fight, fighting here.");
            End(m);
            return;
        }
        bool const arrived = m.chosen >= 0 && PackResolver::Distance(tank.pos, m.chosenSpot) <= ARRIVED_DISTANCE;
        bool const timedOut = WorldTimer::getMSTimeDiff(m.sinceMs, world.refreshedAtMs) > MOVE_TIMEOUT_MS;
        if (!arrived && !timedOut)
            return;
        if (timedOut && !arrived)
            plan.notes.push_back(name + ": moving the fight took too long, fighting from here.");
        End(m);
    }

    void FightSpotAssigner::PlanAhead(WorldModel const& world, TeamPlan& plan) const
    {
        FightSpotPlan& m = plan.fightSpot;
        PullPlan const& pull = plan.pull;
        // Brought back or fought at a corner: the pull's own spot (doc 9, answer 3).
        if (pull.phase == PullPhase::None || pull.phase == PullPhase::Return || pull.returnToSpot || pull.corner)
        {
            m.ClearPlanned();
            return;
        }
        // Judged before the opener; afterwards the choice is kept (the members took their places).
        bool const opened = pull.phase != PullPhase::WaitReady &&
            (pull.phase != PullPhase::Approach || world.HasPartyEnemy());
        if (opened)
            return;
        Position3 at;
        if (!pull.PullFightSpot(at))
            return;
        if (m.plannedJudged && m.plannedPullMs == pull.plannedAtMs && PackResolver::Distance(m.plannedFor, at) <= REJUDGE_YD)
            return;
        PartyMemberFacts const* tank = world.FindMember(pull.tank);
        if (!tank || !world.field)
            return;     // the field covers the pull's spot once the pull runs (PositionFieldBuilder)

        FightSpot::Request r;
        r.at = at;
        r.remainingSec = pull.expectedFightSec > 0.0f ? pull.expectedFightSec : RouteGuard::PULL_FALLBACK_FIGHT_SEC;
        r.patrolWithinSec = FLT_MAX;
        r.walkFromAt = true;
        FightSpot::Choice const c = FightSpot::Choose(world, plan, *tank, r);
        m.ClearPlanned();
        m.plannedJudged = true;
        m.plannedFor = at;
        m.plannedPullMs = pull.plannedAtMs;
        if (c.spots.empty())
            return;
        m.planned = true;
        m.plannedSpots = c.spots;
        m.plannedBenefit = c.hereLoss - c.bestLoss;
        m.plannedCost = c.bestWalk;
        m.plannedWhy = c.why;
        plan.notes.push_back(Reason(c) + Format(": fighting about %.0f yd from the pull's spot (loses %.1f there, %.1f at the spot + %.1f walking).",
            PackResolver::Distance(at, c.spots.front()), c.hereLoss, c.bestLoss, c.bestWalk));
    }

    void FightSpotAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        FightSpotPlan& m = plan.fightSpot;
        PlanAhead(world, plan);
        if (!world.HasPartyEnemy())
        {
            // new fight, new judgements (the pull's plan for it stays)
            End(m);
            m.tank.Clear();
            m.patrol.Clear();
            m.lastNote.clear();
            return;
        }
        // The retreat and the pull's walk out and back own the tank's movement.
        if (plan.retreat.active || plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
        {
            End(m);
            return;
        }
        PartyMemberFacts const* tank = Tanks::Main(world, true);
        if (!tank || !tank->CanAct())
        {
            End(m);
            return;
        }

        if (m.moving)
        {
            Continue(world, plan, *tank);
            return;
        }

        // The tank first holds the fight: with most enemies on someone else, moving leaves them
        // hitting their victims (strat-20261108-200046: the move began with one of six enemies
        // on the tank).
        if (!HoldsMost(world, tank->guid))
            return;

        // The pull's plan (PlanAhead): the move starts once the pull no longer places the tank.
        if (m.planned)
        {
            if (plan.pull.PlacesTank())
                return;
            m.moving = true;
            m.tank = tank->guid;
            m.patrol.Clear();
            m.from = tank->pos;
            m.spots = m.plannedSpots;
            m.benefit = m.plannedBenefit;
            m.cost = m.plannedCost;
            m.why = "planned before the pull: " + m.plannedWhy;
            m.sinceMs = world.refreshedAtMs;
            m.chosen = -1;
            m.lastNote.clear();
            m.ClearPlanned();
            m.plannedJudged = true;     // (not judged again for this pull)
            m.plannedPullMs = plan.pull.plannedAtMs;
            return;
        }

        // While the pull places the tank, only patrols coming soon count (judged again later).
        float const within = plan.pull.PlacesTank() ? PULL_PLACING_DEFER_SEC : FLT_MAX;
        FightSpot::Choice const c = FightSpot::Choose(world, plan, *tank, FightSpot::InFight(world, *tank, within));
        if (c.hereLoss <= 0.0f)
        {
            m.lastNote.clear();
            return;
        }
        std::string const reason = Reason(c);
        if (c.spots.empty())
        {
            std::string const note = reason + Format(" (loses %.1f here); no better spot within %.0f yd, fighting here.",
                c.hereLoss, FightSpot::MAX_YD);
            if (note != m.lastNote)
                plan.notes.push_back(note);
            m.lastNote = note;
            return;
        }

        m.moving = true;
        m.tank = tank->guid;
        m.patrol = c.patrol ? c.patrol->guid : ObjectGuid();
        m.from = tank->pos;
        m.spots = c.spots;
        m.benefit = c.hereLoss - c.bestLoss;
        m.cost = c.bestWalk;
        m.why = c.why;
        m.sinceMs = world.refreshedAtMs;
        m.chosen = -1;
        m.lastNote.clear();
        plan.notes.push_back(reason + Format(": moving the fight about %.0f yd (loses %.1f here, %.1f there + %.1f walking).",
            PackResolver::Distance(tank->pos, m.spots.front()), c.hereLoss, c.bestLoss, c.bestWalk));
    }
}
