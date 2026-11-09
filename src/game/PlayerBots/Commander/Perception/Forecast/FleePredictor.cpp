#include "Commander/Perception/Forecast/FleePredictor.h"
#include "Commander/Perception/Forecast/AssistPredictor.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Creature.h"
#include "Map.h"
#include "PathFinder.h"
#include "Timer.h"
#include "World.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <list>

namespace Commander
{
    namespace
    {
        // Creature::CallForHelp's rules (CallOfHelpCreatureInRangeDo) without the sight check:
        // idle, friendly to the runner (any faction), willing to attack its victim.
        class FleeCallCheck
        {
        public:
            FleeCallCheck(Creature const* runner, Unit const* victim, float range)
                : m_runner(runner), m_victim(victim), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c != m_runner && m_runner->IsWithinDistInMap(c, m_range) &&
                    c->CanBeTargetedByCallForHelp(m_runner, m_victim, false) &&
                    c->CanRespondToCallForHelpAgainst(m_victim);
            }

        private:
            Creature const* m_runner;
            Unit const* m_victim;
            float m_range;
        };

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        bool Contains(std::vector<ObjectGuid> const& guids, ObjectGuid guid)
        {
            return std::find(guids.begin(), guids.end(), guid) != guids.end();
        }

        float ReachYd(Creature const* runner)
        {
            return std::min(runner->GetSpeed(MOVE_RUN) * FleePredictor::FleeSec(), FleePredictor::MAX_RUN_YD);
        }

        struct Caller
        {
            Creature* creature = nullptr;
            float distance = 0.0f;      // straight in sight, else along the navmesh
        };

        // Worst case: every idle creature the run can come within CALL_YD of (header), each
        // with what it brings, nearest caller first.
        std::vector<FleeJoiner> PredictJoiners(Creature* runner, Unit* victim, WorldModel const& world,
                                               PartyMemberFacts const& tank)
        {
            float const radius = ReachYd(runner) + FleePredictor::CALL_YD;
            std::list<Creature*> found;
            FleeCallCheck check(runner, victim, radius);
            MaNGOS::CreatureListSearcher<FleeCallCheck> searcher(found, check);
            Cell::VisitGridObjects(runner, searcher, radius);

            std::vector<Creature*> inRange(found.begin(), found.end());
            std::sort(inRange.begin(), inRange.end(), [runner](Creature const* a, Creature const* b)
            {
                return runner->GetDistance(a) < runner->GetDistance(b);
            });

            // A creature out of sight is reached around the corner, if the walk is short enough.
            // Over the path budget (the farthest ones) they are left out.
            std::vector<Caller> callers;
            uint32 paths = 0;
            for (Creature* c : inRange)
            {
                if (runner->IsWithinLOSInMap(c))
                {
                    callers.push_back({ c, runner->GetDistance(c) });
                    continue;
                }
                if (paths >= FleePredictor::MAX_PATHS_PER_ENEMY)
                    continue;
                ++paths;
                Position3 const to = PositionOf(c);
                PathInfo path(runner);
                path.calculate(to.x, to.y, to.z);
                uint32 const type = path.getPathType();
                if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH))
                    continue;
                if (path.Length() <= radius && SafeRoute::PathReaches(runner->GetMap(), path, to))
                    callers.push_back({ c, path.Length() });
            }
            std::stable_sort(callers.begin(), callers.end(), [](Caller const& a, Caller const& b)
            {
                return a.distance < b.distance;
            });

            std::vector<ObjectGuid> seen{ runner->GetObjectGuid() };
            for (EnemyFacts const& e : world.enemies)
                seen.push_back(e.guid);
            std::vector<FleeJoiner> out;
            for (Caller const& caller : callers)
            {
                if (Contains(seen, caller.creature->GetObjectGuid()))
                    continue;
                for (AssistPredictor::Joiner const& j : AssistPredictor::Expand({ caller.creature }, victim, seen))
                {
                    seen.push_back(j.guid);
                    Creature const* c = runner->GetMap()->GetAnyTypeCreature(j.guid);
                    if (!c || !c->IsAlive())
                        continue;
                    FightLoad::Foe const foe = FightLoad::FoeOf(c, tank);
                    FleeJoiner fj;
                    fj.guid = j.guid;
                    fj.needYd = std::max(0.0f, caller.distance - FleePredictor::CALL_YD);
                    fj.health = foe.health;
                    fj.dps = foe.dps;
                    out.push_back(fj);
                }
            }
            return out;
        }

        // Keeps hitting a runner: ranged damage dealers (pets chase like melee).
        bool IsRanged(PartyMemberFacts const& m)
        {
            return m.role == ROLE_RANGE_DPS;
        }
    }

    float FleePredictor::FleeSec()
    {
        return sWorld.getConfig(CONFIG_UINT32_CREATURE_FAMILY_FLEE_DELAY) / 1000.0f;
    }

    float FleePredictor::HealthAtRun(EnemyFacts const& e)
    {
        float const at = e.flee.atPct / 100.0f * float(e.maxHealth);
        return e.flee.fleeing ? float(e.health) : std::min(float(e.health), at);
    }

    float FleePredictor::RunKillSec(WorldModel const& world, EnemyFacts const& e, float fleeSec)
    {
        uint32 ranged = 0;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.CanAct() && IsRanged(m))
                ++ranged;
        float const dealers = float(std::max(1u, world.rates.damageDealers));
        float const dps = Value::DealtDps(world);
        float const rangedDps = dps * std::min(1.0f, float(ranged) / dealers);
        float health = HealthAtRun(e);
        // Already running: the melee have lost it.
        if (!e.flee.fleeing)
            health -= (dps - rangedDps) * MELEE_CONTACT_SEC;
        if (health <= 0.0f)
            return 0.0f;
        if (rangedDps <= 0.0f)
            return fleeSec;
        return std::min(fleeSec, health / rangedDps);
    }

    float FleePredictor::PointsWithin(WorldModel const& world, EnemyFacts const& e, float reachYd)
    {
        FleeFacts const& f = e.flee;
        PartyMemberFacts const* tank = Tanks::Main(world);
        FightLoad::Input asIs;
        if (!tank || !FightLoad::FromWorld(world, asIs))
            return 0.0f;
        float const arriveSec = (f.fleeing ? 0.0f : f.secToFlee) + JOIN_SEC;
        FightLoad::Input with = asIs;
        for (FleeJoiner const& j : f.joiners)
        {
            if (j.needYd > reachYd)
                continue;
            FightLoad::Foe foe;
            foe.guid = j.guid;
            foe.health = j.health;
            foe.dps = j.dps;
            foe.arriveSec = arriveSec;
            foe.sameFight = true;
            with.foes.push_back(foe);
        }
        if (with.foes.size() == asIs.foes.size())
            return 0.0f;
        float const penalty = Value::DeathPenalty(*tank);
        float const base = FightLoad::Loss(world, FightLoad::Simulate(asIs, "flee: as is"), penalty);
        return std::max(0.0f, FightLoad::Loss(world, FightLoad::Simulate(with, "flee: with the joiners"), penalty) - base);
    }

    void FleePredictor::Annotate(Map* map, WorldModel& world, uint32 nowMs)
    {
        // Forget what left our lists (dead, evaded: an evade resets the flee event).
        auto onLists = [&world](ObjectGuid guid) { return world.FindEnemy(guid) != nullptr; };
        m_fled.erase(std::remove_if(m_fled.begin(), m_fled.end(), [&](ObjectGuid g) { return !onLists(g); }), m_fled.end());
        m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(), [&](Entry const& c) { return !onLists(c.enemy); }), m_cache.end());

        PartyMemberFacts const* tank = Tanks::Main(world);
        float const fleeSec = FleeSec();
        uint32 searches = 0;
        for (EnemyFacts& e : world.enemies)
        {
            e.flee = FleeFacts();
            if (e.onAllyOnly)
                continue;
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry);
            if (!profile || profile->fleeAtHealthPct <= 0.0f)
                continue;
            Creature* runner = map->GetAnyTypeCreature(e.guid);
            if (!runner || !runner->IsAlive())
                continue;

            FleeFacts& f = e.flee;
            f.atPct = profile->fleeAtHealthPct;
            f.fleeing = runner->HasUnitState(UNIT_STATE_FLEEING) && !runner->HasAuraType(SPELL_AURA_MOD_FEAR);
            if (f.fleeing && !Contains(m_fled, e.guid))
                m_fled.push_back(e.guid);
            f.spent = !f.fleeing && Contains(m_fled, e.guid);
            f.prevented = runner->HasAuraType(SPELL_AURA_PREVENTS_FLEEING);
            if (e.healthPct > f.atPct)
                f.secToFlee = Value::EnemyTimeToDie(world, e) * (e.healthPct - f.atPct) / e.healthPct;

            if (f.spent || f.prevented || (!f.fleeing && e.healthPct > f.atPct + WINDOW_PCT))
                continue;
            Unit* victim = runner->GetVictim();
            if (!victim || !tank)
                continue;

            Entry* entry = nullptr;
            for (Entry& c : m_cache)
                if (c.enemy == e.guid)
                    entry = &c;
            bool const due = !entry || WorldTimer::getMSTimeDiff(entry->atMs, nowMs) >= MAX_AGE_MS ||
                             PackResolver::Distance(entry->at, e.pos) >= MOVED_YD;
            if (due && searches < MAX_SEARCHES_PER_REFRESH)
            {
                ++searches;
                if (!entry)
                {
                    m_cache.emplace_back();
                    entry = &m_cache.back();
                    entry->enemy = e.guid;
                }
                entry->at = e.pos;
                entry->atMs = nowMs;
                entry->joiners = PredictJoiners(runner, victim, world, *tank);
            }
            if (!entry)
                continue;   // over the search budget, never searched: next refresh
            f.predicted = true;
            f.joiners = entry->joiners;
            f.runSpeed = runner->GetSpeed(MOVE_RUN);
            f.reachYd = ReachYd(runner);
            f.points = PointsWithin(world, e, f.reachYd);
            f.runKillSec = RunKillSec(world, e, fleeSec);
            f.focusedReachYd = std::min(f.reachYd, f.runSpeed * f.runKillSec);
            f.focusedPoints = f.focusedReachYd >= f.reachYd ? f.points : PointsWithin(world, e, f.focusedReachYd);
        }
    }
}
