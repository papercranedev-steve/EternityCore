#include "Commander/Coordination/Situation/FightSpot/FightSpotAssigner.h"
#include "Commander/Coordination/Situation/FightSpot/FightSpot.h"
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

    void FightSpotAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        FightSpotPlan& m = plan.fightSpot;
        if (!world.HasPartyEnemy())
        {
            m = FightSpotPlan();        // new fight, new judgements
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

        // While the pull places the tank, only patrols coming soon count (judged again later).
        float const within = plan.pull.PlacesTank() ? PULL_PLACING_DEFER_SEC : FLT_MAX;
        FightSpot::Choice const c = FightSpot::Choose(world, plan, *tank, within);
        if (c.hereLoss <= 0.0f)
        {
            m.lastNote.clear();
            return;
        }
        std::string const name = c.patrol ? NameOf(c.patrol->entry) : std::string();
        std::string const reason = c.herePatrols > 0.0f && c.hereHolds > 0.0f ? name + " walks in and a control is next to the pile"
                                 : c.herePatrols > 0.0f ? name + " walks into the fight"
                                 : "a control that breaks on damage is next to the pile";
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
