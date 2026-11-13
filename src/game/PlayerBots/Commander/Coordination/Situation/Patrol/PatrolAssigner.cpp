#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/FightSpot/FightSpot.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Timer.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Commander
{
    namespace
    {
        std::string NameOf(uint32 entry, char const* fallback = "patrol")
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile ? profile->name : std::string(fallback);
        }

        // At least half of the acting, uncontrolled enemies fighting the party hit `tank`.
        bool HoldsMost(WorldModel const& world, ObjectGuid tank)
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

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        void End(PatrolMovePlan& m)
        {
            m.active = false;
            m.phase = PatrolPhase::Move;
            m.spots.clear();
            m.chosen = -1;
            m.avoid.clear();
            m.lures.clear();
            m.kill.clear();
        }

        // Stands back and casts at its victim instead of following it.
        bool StandsBack(WorldModel const& world, EnemyFacts const& e)
        {
            if (!e.acting || e.crowdControlled || !e.health)
                return false;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || PackResolver::Distance(e.pos, victim->pos) <= PatrolAssigner::LURE_MIN_DISTANCE)
                return false;
            return !e.meleeEnabled || LosBreak::IsRangedCaster(e.entry);
        }

        PatrolLure* LureOfHider(PatrolMovePlan& m, ObjectGuid hider)
        {
            for (PatrolLure& l : m.lures)
                if (l.hider == hider)
                    return &l;
            return nullptr;
        }

        PatrolLure* LureWithCaster(PatrolMovePlan& m, ObjectGuid caster)
        {
            for (PatrolLure& l : m.lures)
                if (Contains(l.casters, caster))
                    return &l;
            return nullptr;
        }

        // The caster no longer needs the lure: gone, out of the path, turned to someone else,
        // or came close to the hider.
        bool CasterCame(WorldModel const& world, PatrolMovePlan const& m, PatrolLure const& l, ObjectGuid caster)
        {
            EnemyFacts const* e = world.FindEnemy(caster);
            if (!e || !e->acting || e->crowdControlled || !Contains(m.avoid, caster) || e->victim != l.hider)
                return true;
            PartyMemberFacts const* hider = world.FindMember(l.hider);
            return !hider || PackResolver::Distance(e->pos, hider->pos) <= PatrolAssigner::LURE_ARRIVED;
        }

        // Guard: lures for casters standing back in the path, the rest goes to `kill`.
        void UpdateLures(WorldModel const& world, PatrolMovePlan& m, std::vector<std::string>& notes)
        {
            uint32 const now = world.refreshedAtMs;
            for (PatrolLure& l : m.lures)
            {
                PartyMemberFacts const* hider = world.FindMember(l.hider);
                if (l.state == PatrolLureState::Search && (!hider || !hider->CanAct()))
                    l.state = PatrolLureState::NoPoint;
                if (l.state != PatrolLureState::Active)
                    continue;
                bool const allCame = std::all_of(l.casters.begin(), l.casters.end(),
                    [&](ObjectGuid g) { return CasterCame(world, m, l, g); });
                if (allCame || !hider || !hider->CanAct())
                    l.state = PatrolLureState::Done;
                else if (WorldTimer::getMSTimeDiff(l.sinceMs, now) > PatrolAssigner::LURE_TIMEOUT_MS)
                {
                    l.state = PatrolLureState::Failed;
                    EnemyFacts const* first = world.FindEnemy(l.casters.front());
                    notes.push_back(Format("%s did not come out of the patrol's path in %u s: killing it from range.",
                        NameOf(first ? first->entry : 0, "The caster").c_str(), PatrolAssigner::LURE_TIMEOUT_MS / 1000));
                }
            }

            m.kill.clear();
            for (ObjectGuid g : m.avoid)
            {
                EnemyFacts const* e = world.FindEnemy(g);
                if (!e || !StandsBack(world, *e))
                    continue;
                if (PatrolLure* l = LureWithCaster(m, g))
                {
                    if (l->state != PatrolLureState::Search && l->state != PatrolLureState::Active)
                        m.kill.push_back(g);
                    continue;
                }
                // Its victim hides; only a bot can be told to, and only once per fight.
                PartyMemberFacts const* victim = world.FindMember(e->victim);
                bool const canHide = victim && victim->isBot && !victim->isPet && victim->CanAct();
                PatrolLure* own = canHide ? LureOfHider(m, victim->guid) : nullptr;
                if (own && own->state == PatrolLureState::Search && own->casters.size() < PatrolAssigner::MAX_LURE_CASTERS)
                {
                    own->casters.push_back(g);
                    continue;
                }
                if (!canHide || own)
                {
                    m.kill.push_back(g);
                    continue;
                }
                PatrolLure l;
                l.hider = victim->guid;
                l.casters.push_back(g);
                m.lures.push_back(l);
            }
        }
    }

    PatrolFacts const* PatrolAssigner::FindPatrol(WorldModel const& world, PatrolMovePlan const& m)
    {
        for (PatrolFacts const& p : world.patrols)
            if (p.guid == m.patrol)
                return &p;
        return nullptr;
    }

    void PatrolAssigner::Clear(TeamPlan& plan) const
    {
        plan.patrolMove = PatrolMovePlan();
    }

    void PatrolAssigner::Continue(WorldModel const& world, TeamPlan& plan, PartyMemberFacts const& tank) const
    {
        PatrolMovePlan& m = plan.patrolMove;
        PatrolFacts const* patrol = FindPatrol(world, m);
        // Its patrol joined the fight (now an enemy), died, or walked out of the scan. A move for
        // a control next to the pile has no patrol.
        if (m.tank != tank.guid || (!m.patrol.IsEmpty() && !patrol))
        {
            End(m);
            return;
        }
        std::string const name = patrol ? NameOf(patrol->entry) : std::string("The fight");
        float const remaining = Value::FightRemainingSec(world);

        m.avoid.clear();
        if (patrol)
            for (EnemyFacts const& e : world.enemies)
                if (e.health && PatrolZone::Comes(world, *patrol, e.pos, remaining))
                    m.avoid.push_back(e.guid);

        if (m.phase == PatrolPhase::Move)
        {
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
                plan.notes.push_back("No reachable spot away from the patrol's path, fighting here.");
                End(m);
                return;
            }
            bool const arrived = m.chosen >= 0 && PackResolver::Distance(tank.pos, m.chosenSpot) <= ARRIVED_DISTANCE;
            bool const timedOut = WorldTimer::getMSTimeDiff(m.sinceMs, world.refreshedAtMs) > MOVE_TIMEOUT_MS;
            if (!arrived && !timedOut)
                return;
            if (timedOut && !arrived)
                plan.notes.push_back(name + ": moving the fight took too long, fighting from here.");
            // No patrol to guard against: done (the next check looks again from here).
            if (!patrol)
            {
                End(m);
                return;
            }
            m.phase = PatrolPhase::Guard;
            m.guardSinceMs = world.refreshedAtMs;
        }

        // Guard until the patrol walked past: the old fight spot is clear of its walk for the
        // rest of the fight and no enemy stands in it.
        if ((!PatrolZone::Comes(world, *patrol, m.tankStart, remaining) && m.avoid.empty()) ||
            WorldTimer::getMSTimeDiff(m.guardSinceMs, world.refreshedAtMs) > GUARD_TIMEOUT_MS)
        {
            End(m);
            return;
        }
        UpdateLures(world, m, plan.notes);
    }

    void PatrolAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        PatrolMovePlan& m = plan.patrolMove;
        if (!world.HasPartyEnemy())
        {
            m = PatrolMovePlan();       // new fight, new judgements
            return;
        }
        // The retreat and the pull own the tank's movement.
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

        if (m.active)
        {
            Continue(world, plan, *tank);
            return;
        }

        // The pull still places the tank (waiting for the pack, or hiding from casters standing
        // back): one place at a time. A patrol arriving later is judged once that is over, from
        // where the fight then is; the line of sight point already avoids patrol paths
        // (strat-20261107-143623: hiding point, then 15 yd off the path, then the guard's hiding
        // point; the casters saw the tank most of the time). Reads the pull as PullAssigner
        // left it, this refresh or the last one (one refresh late at worst either way).
        bool const pullPlacing = plan.pull.phase == PullPhase::Settle &&
            (plan.pull.awaitArrival || plan.pull.losBreak == PullLosBreak::Active);
        // The tank first holds the fight: with most enemies on someone else, moving leaves them
        // hitting their victims (strat-20261108-200046: the move began with one of six enemies
        // on the tank). Patrols are judged again once it holds them (not marked judged).
        if (!HoldsMost(world, tank->guid))
            return;

        // The tank's place against the field cells around it (FightSpot.h), CHECK_MS apart.
        if (m.checkedAtMs && WorldTimer::getMSTimeDiff(m.checkedAtMs, world.refreshedAtMs) < CHECK_MS)
            return;
        m.checkedAtMs = world.refreshedAtMs;
        // While the pull places the tank, only patrols coming soon count (judged again later).
        float const within = pullPlacing ? PULL_PLACING_DEFER_SEC : FLT_MAX;
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

        m.active = true;
        m.phase = PatrolPhase::Move;
        m.patrol = c.patrol ? c.patrol->guid : ObjectGuid();
        m.tank = tank->guid;
        m.tankStart = tank->pos;
        m.spots = c.spots;
        m.benefit = c.hereLoss - c.bestLoss;
        m.cost = c.bestWalk;
        m.why = c.why;
        m.sinceMs = world.refreshedAtMs;
        m.guardSinceMs = 0;
        m.chosen = -1;
        m.avoid.clear();
        m.lures.clear();
        m.kill.clear();
        m.lastNote.clear();
        plan.notes.push_back(reason + Format(": moving the fight about %.0f yd (loses %.1f here, %.1f there + %.1f walking).",
            PackResolver::Distance(tank->pos, m.spots.front()), c.hereLoss, c.bestLoss, c.bestWalk));
    }
}
