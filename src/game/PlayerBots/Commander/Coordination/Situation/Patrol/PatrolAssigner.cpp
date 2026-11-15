#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Values/SpotCost.h"
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

        void End(PatrolGuardPlan& m)
        {
            m.active = false;
            m.patrol.Clear();
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

        LosLure* LureOfHider(PatrolGuardPlan& m, ObjectGuid hider)
        {
            for (LosLure& l : m.lures)
                if (l.hider == hider)
                    return &l;
            return nullptr;
        }

        LosLure* LureWithCaster(PatrolGuardPlan& m, ObjectGuid caster)
        {
            for (LosLure& l : m.lures)
                if (Contains(l.casters, caster))
                    return &l;
            return nullptr;
        }

        // The caster no longer needs the lure: gone, out of the path, turned to someone else,
        // or came close to the hider.
        bool CasterCame(WorldModel const& world, PatrolGuardPlan const& m, LosLure const& l, ObjectGuid caster)
        {
            EnemyFacts const* e = world.FindEnemy(caster);
            if (!e || !e->acting || e->crowdControlled || !Contains(m.avoid, caster) || e->victim != l.hider)
                return true;
            PartyMemberFacts const* hider = world.FindMember(l.hider);
            return !hider || PackResolver::Distance(e->pos, hider->pos) <= PatrolAssigner::LURE_ARRIVED;
        }

        // Lures for casters standing back in the path, the rest goes to `kill`.
        void UpdateLures(WorldModel const& world, PatrolGuardPlan& m, std::vector<std::string>& notes)
        {
            uint32 const now = world.refreshedAtMs;
            for (LosLure& l : m.lures)
            {
                PartyMemberFacts const* hider = world.FindMember(l.hider);
                if (l.state == LosLureState::Search && (!hider || !hider->CanAct()))
                    l.state = LosLureState::NoPoint;
                if (l.state != LosLureState::Active)
                    continue;
                bool const allCame = std::all_of(l.casters.begin(), l.casters.end(),
                    [&](ObjectGuid g) { return CasterCame(world, m, l, g); });
                if (allCame || !hider || !hider->CanAct())
                    l.state = LosLureState::Done;
                else if (WorldTimer::getMSTimeDiff(l.sinceMs, now) > PatrolAssigner::LURE_TIMEOUT_MS)
                {
                    l.state = LosLureState::Failed;
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
                if (LosLure* l = LureWithCaster(m, g))
                {
                    if (l->state != LosLureState::Search && l->state != LosLureState::Active)
                        m.kill.push_back(g);
                    continue;
                }
                // Its victim hides; only a bot can be told to, and only once per fight.
                PartyMemberFacts const* victim = world.FindMember(e->victim);
                bool const canHide = victim && victim->isBot && !victim->isPet && victim->CanAct();
                LosLure* own = canHide ? LureOfHider(m, victim->guid) : nullptr;
                if (own && own->state == LosLureState::Search && own->casters.size() < PatrolAssigner::MAX_LURE_CASTERS)
                {
                    own->casters.push_back(g);
                    continue;
                }
                if (!canHide || own)
                {
                    m.kill.push_back(g);
                    continue;
                }
                LosLure l;
                l.hider = victim->guid;
                l.casters.push_back(g);
                m.lures.push_back(l);
            }
        }

        // The enemies `p` comes to before the fight ends.
        std::vector<ObjectGuid> InPath(WorldModel const& world, PatrolFacts const& p, float remaining)
        {
            std::vector<ObjectGuid> out;
            for (EnemyFacts const& e : world.enemies)
                if (e.health && PatrolZone::Comes(world, p, e.pos, remaining))
                    out.push_back(e.guid);
            return out;
        }

        // Where the pile is: the fight spot's target while the tank walks there, else the tank
        // (FightSpotAssigner, this refresh or the last one).
        Position3 PileAt(TeamPlan const& plan, PartyMemberFacts const& tank)
        {
            Position3 at = tank.pos;
            if (plan.fightSpot.Moves(tank.guid))
                plan.fightSpot.Target(at);
            return at;
        }

        // What the patrol costs if the fight goes to one of `enemies`.
        float WorstJoin(WorldModel const& world, PartyMemberFacts const& tank, PatrolFacts const& p,
                        std::vector<ObjectGuid> const& enemies)
        {
            float worst = 0.0f;
            for (ObjectGuid g : enemies)
                if (EnemyFacts const* e = world.FindEnemy(g))
                    worst = std::max(worst, SpotCost::PatrolJoinPoints(world, tank, p, e->pos, Value::FightRemainingSec(world), FLT_MAX));
            return worst;
        }
    }

    PatrolFacts const* PatrolAssigner::FindPatrol(WorldModel const& world, PatrolGuardPlan const& m)
    {
        for (PatrolFacts const& p : world.patrols)
            if (p.guid == m.patrol)
                return &p;
        return nullptr;
    }

    void PatrolAssigner::Clear(TeamPlan& plan) const
    {
        plan.patrolGuard = PatrolGuardPlan();
    }

    void PatrolAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        PatrolGuardPlan& m = plan.patrolGuard;
        if (!world.HasPartyEnemy())
        {
            m = PatrolGuardPlan();      // new fight, new judgements
            return;
        }
        // The retreat and the pull own the tank's movement.
        if (plan.retreat.active || plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
        {
            End(m);
            return;
        }
        PartyMemberFacts const* tank = Tanks::Main(world, true);
        if (!tank || !tank->CanAct() || (m.active && m.tank != tank->guid))
        {
            End(m);
            return;
        }
        float const remaining = Value::FightRemainingSec(world);
        Position3 const pile = PileAt(plan, *tank);

        if (m.active)
        {
            PatrolFacts const* patrol = FindPatrol(world, m);
            // Joined the fight or left the scan; or it walks into the pile now (the fight spot's).
            if (!patrol || SpotCost::PatrolJoinPoints(world, *tank, *patrol, pile, remaining, FLT_MAX) > 0.0f)
            {
                End(m);
                return;
            }
            m.avoid = InPath(world, *patrol, remaining);
            if (m.avoid.empty())
            {
                End(m);
                return;
            }
            if (WorldTimer::getMSTimeDiff(m.sinceMs, world.refreshedAtMs) > GUARD_TIMEOUT_MS)
            {
                m.timedOut.push_back(m.patrol);
                End(m);
                return;
            }
            UpdateLures(world, m, plan.notes);
            return;
        }

        for (PatrolFacts const& p : world.patrols)
        {
            if (!PatrolZone::Counts(p) || Contains(m.timedOut, p.guid))
                continue;
            if (SpotCost::PatrolJoinPoints(world, *tank, p, pile, remaining, FLT_MAX) > 0.0f)
                continue;
            std::vector<ObjectGuid> avoid = InPath(world, p, remaining);
            if (avoid.empty())
                continue;
            m.active = true;
            m.patrol = p.guid;
            m.tank = tank->guid;
            m.sinceMs = world.refreshedAtMs;
            m.points = WorstJoin(world, *tank, p, avoid);
            m.avoid = std::move(avoid);
            m.lures.clear();
            UpdateLures(world, m, plan.notes);
            if (!Contains(m.announced, p.guid))
            {
                m.announced.push_back(p.guid);
                plan.notes.push_back(NameOf(p.entry) + Format(" walks where %u of the enemies stand: nobody goes over to them.",
                    uint32(m.avoid.size())));
            }
            return;
        }
    }
}
