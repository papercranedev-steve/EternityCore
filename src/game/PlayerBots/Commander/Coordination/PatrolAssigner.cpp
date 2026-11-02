#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/LosBreak.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/CreatureProfile.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Commander
{
    namespace
    {
        // Living members (players and bots, no pets), each shifted by (dx, dy): where they
        // stand after the tank moved that far (they keep their place around the tank).
        std::vector<Position3> Members(WorldModel const& world, float dx, float dy)
        {
            std::vector<Position3> out;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive)
                    continue;
                Position3 p = m.pos;
                p.x += dx;
                p.y += dy;
                out.push_back(p);
            }
            return out;
        }

        // First time (seconds from the refresh) the patrol's predicted walk comes within
        // `radius` of any of `members`, -1 = not within the track.
        float FirstTouch(PatrolFacts const& p, std::vector<Position3> const& members, float radius)
        {
            for (size_t i = 0; i < p.track.size(); ++i)
                for (Position3 const& m : members)
                    if (PackResolver::Distance(p.track[i], m) <= radius)
                        return float(i) * p.trackStepSec;
            return -1.0f;
        }

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

        // Ring points around the tank where the patrol's walk never comes near the party moved
        // along with the tank, nearest ring first; within a ring, farthest from where the
        // patrol arrives first.
        std::vector<Position3> Spots(WorldModel const& world, PatrolFacts const& p, Position3 const& tank,
                                     Position3 const& arrivesAt, float radius)
        {
            std::vector<Position3> spots;
            float const away = std::atan2(tank.y - arrivesAt.y, tank.x - arrivesAt.x);
            for (float d : PatrolAssigner::SPOT_DISTANCES)
            {
                for (uint32 k = 0; k < PatrolAssigner::SPOT_ANGLES && spots.size() < PatrolAssigner::MAX_SPOTS; ++k)
                {
                    // 0, +1, -1, +2, -2 ... steps from straight away from the patrol.
                    int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                    float const angle = away + 2.0f * float(M_PI) * step / PatrolAssigner::SPOT_ANGLES;
                    Position3 s;
                    s.x = tank.x + d * std::cos(angle);
                    s.y = tank.y + d * std::sin(angle);
                    s.z = tank.z;
                    if (FirstTouch(p, Members(world, s.x - tank.x, s.y - tank.y), radius) < 0.0f)
                        spots.push_back(s);
                }
            }
            return spots;
        }

        // Every bot but the tank walks the tank's shift (TeamPlan.h, PatrolFollower).
        std::vector<PatrolFollower> Followers(WorldModel const& world, ObjectGuid tank)
        {
            std::vector<PatrolFollower> out;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.alive || m.guid == tank)
                    continue;
                if (m.role != ROLE_HEALER && m.role != ROLE_RANGE_DPS && m.role != ROLE_MELEE_DPS)
                    continue;
                PatrolFollower f;
                f.guid = m.guid;
                f.start = m.pos;
                out.push_back(f);
            }
            return out;
        }

        // Every follower that can still walk its shift is there (the tank's spot is chosen).
        bool FollowersArrived(WorldModel const& world, PatrolMovePlan const& m)
        {
            for (PatrolFollower const& f : m.followers)
            {
                if (f.state < 0)
                    continue;
                PartyMemberFacts const* member = world.FindMember(f.guid);
                if (!member || !member->CanAct())
                    continue;
                if (f.state == 0 || PackResolver::Distance(member->pos, f.target) > PatrolAssigner::ARRIVED_DISTANCE)
                    return false;
            }
            return true;
        }

        void End(PatrolMovePlan& m)
        {
            m.active = false;
            m.phase = PatrolPhase::Move;
            m.followers.clear();
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

    float PatrolAssigner::ZoneRadius(PatrolFacts const& p)
    {
        return p.reach + ZONE_MARGIN;
    }

    bool PatrolAssigner::InPath(PatrolFacts const& p, Position3 const& pos, float withinSec)
    {
        float const t = FirstTouch(p, { pos }, ZoneRadius(p));
        return t >= 0.0f && t < withinSec;
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
        // Joined the fight (now an enemy), died, or walked out of the scan.
        if (m.tank != tank.guid || !patrol)
        {
            End(m);
            return;
        }
        std::string const name = NameOf(patrol->entry);
        float const remaining = Value::FightRemainingSec(world);

        m.avoid.clear();
        for (EnemyFacts const& e : world.enemies)
            if (e.health && InPath(*patrol, e.pos, remaining))
                m.avoid.push_back(e.guid);

        if (m.phase == PatrolPhase::Move)
        {
            if (m.chosen == -2)
            {
                plan.notes.push_back("No reachable spot away from the patrol's path, fighting here.");
                End(m);
                return;
            }
            bool const arrived = m.chosen >= 0 && PackResolver::Distance(tank.pos, m.chosenSpot) <= ARRIVED_DISTANCE &&
                                 FollowersArrived(world, m);
            bool const timedOut = WorldTimer::getMSTimeDiff(m.sinceMs, world.refreshedAtMs) > MOVE_TIMEOUT_MS;
            if (!arrived && !timedOut)
                return;
            if (timedOut && !arrived)
                plan.notes.push_back(name + ": moving the fight took too long, staying out of its path from here.");
            m.phase = PatrolPhase::Guard;
            m.guardSinceMs = world.refreshedAtMs;
        }

        // Guard until the patrol walked past: the old fight spot is clear of its walk for the
        // rest of the fight and no enemy stands in it.
        if ((!InPath(*patrol, m.tankStart, remaining) && m.avoid.empty()) ||
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

        float const remaining = Value::FightRemainingSec(world);
        float const dealt = std::max(1.0f, Value::DealtDps(world));
        std::vector<Position3> const members = Members(world, 0.0f, 0.0f);
        for (PatrolFacts const& p : world.patrols)
        {
            if (!p.aggressive || p.noAggro || p.tappedByOther || p.track.empty() || Contains(m.judged, p.guid))
                continue;
            float const radius = ZoneRadius(p);
            float const arrive = FirstTouch(p, members, radius);
            if (arrive < 0.0f || arrive >= remaining)
                continue;
            m.judged.push_back(p.guid);
            std::string const name = NameOf(p.entry);

            // What it costs if it joins: it hits the tank from arrival until the fight is over
            // and it is dead, and the fight lasts its kill time longer.
            float const killSec = float(p.health) / dealt;
            float const hit = p.meleeDps * (1.0f - Value::ArmorReduction(tank->armor, p.level));
            float const benefit = Value::Points(world, hit * (remaining - arrive + killSec)) +
                                  Value::FightExtension(world, killSec);

            Position3 const arrivesAt = p.track[std::min(p.track.size() - 1, size_t(arrive / p.trackStepSec))];
            std::vector<Position3> spots = Spots(world, p, tank->pos, arrivesAt, radius);
            if (spots.empty())
            {
                plan.notes.push_back(Format("%s reaches the fight in about %.0f s (fight about %.0f s left); "
                    "no spot within %.0f yd keeps away from its path, fighting here.", name.c_str(), arrive, remaining,
                    SPOT_DISTANCES[sizeof(SPOT_DISTANCES) / sizeof(float) - 1]));
                continue;
            }
            float const walkSec = PackResolver::Distance(tank->pos, spots.front()) * PATH_FACTOR / RUN_YARDS_PER_SEC;
            float const cost = Value::FightExtension(world, walkSec);
            if (benefit <= cost)
            {
                plan.notes.push_back(Format("%s reaches the fight in about %.0f s; staying (it costs %.1f, "
                    "moving %.1f).", name.c_str(), arrive, benefit, cost));
                continue;
            }

            m.active = true;
            m.phase = PatrolPhase::Move;
            m.patrol = p.guid;
            m.tank = tank->guid;
            m.tankStart = tank->pos;
            m.followers = Followers(world, tank->guid);
            m.spots = std::move(spots);
            m.arriveSec = arrive;
            m.benefit = benefit;
            m.cost = cost;
            m.sinceMs = world.refreshedAtMs;
            m.guardSinceMs = 0;
            m.chosen = -1;
            m.avoid.clear();
            m.lures.clear();
            m.kill.clear();
            plan.notes.push_back(Format("%s reaches the fight in about %.0f s (fight about %.0f s left): "
                "moving the fight about %.0f yd off its path (%.1f vs %.1f).", name.c_str(), arrive, remaining,
                PackResolver::Distance(tank->pos, m.spots.front()), benefit, cost));
            return;
        }
    }
}
