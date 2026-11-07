#include "Commander/Perception/FleePredictor.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
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
            float const fleeSec = sWorld.getConfig(CONFIG_UINT32_CREATURE_FAMILY_FLEE_DELAY) / 1000.0f;
            return std::min(runner->GetSpeed(MOVE_RUN) * fleeSec, FleePredictor::MAX_RUN_YD);
        }

        // Worst case: every idle creature the run can come within CALL_YD of (header).
        std::vector<ObjectGuid> PredictJoiners(Creature* runner, Unit* victim, WorldModel const& world)
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
            std::vector<Creature*> seeds;
            uint32 paths = 0;
            for (Creature* c : inRange)
            {
                if (runner->IsWithinLOSInMap(c))
                {
                    seeds.push_back(c);
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
                    seeds.push_back(c);
            }

            std::vector<ObjectGuid> exclude{ runner->GetObjectGuid() };
            for (EnemyFacts const& e : world.enemies)
                exclude.push_back(e.guid);
            return AssistPredictor::Guids(AssistPredictor::Expand(seeds, victim, exclude));
        }

        // Loss(fight with the joiners arriving `arriveSec` in) - Loss(fight as it is), >= 0.
        float JoinPoints(WorldModel const& world, std::vector<FightLoad::Foe> const& foes, float arriveSec)
        {
            if (foes.empty())
                return 0.0f;
            PartyMemberFacts const* tank = Tanks::Main(world);
            FightLoad::Input asIs;
            if (!tank || !FightLoad::FromWorld(world, asIs))
                return 0.0f;
            float const penalty = Value::DeathPenalty(*tank);
            FightLoad::Input with = asIs;
            for (FightLoad::Foe f : foes)
            {
                f.arriveSec = arriveSec;
                f.sameFight = true;
                with.foes.push_back(f);
            }
            float const base = FightLoad::Loss(world, FightLoad::Simulate(asIs), penalty);
            return std::max(0.0f, FightLoad::Loss(world, FightLoad::Simulate(with), penalty) - base);
        }
    }

    void FleePredictor::Annotate(Map* map, WorldModel& world, uint32 nowMs)
    {
        // Forget what left our lists (dead, evaded: an evade resets the flee event).
        auto onLists = [&world](ObjectGuid guid) { return world.FindEnemy(guid) != nullptr; };
        m_fled.erase(std::remove_if(m_fled.begin(), m_fled.end(), [&](ObjectGuid g) { return !onLists(g); }), m_fled.end());
        m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(), [&](Entry const& c) { return !onLists(c.enemy); }), m_cache.end());

        PartyMemberFacts const* tank = Tanks::Main(world);
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
                entry->joiners = PredictJoiners(runner, victim, world);
                entry->foes.clear();
                for (ObjectGuid g : entry->joiners)
                    if (Creature const* c = map->GetAnyTypeCreature(g))
                        if (c->IsAlive())
                            entry->foes.push_back(FightLoad::FoeOf(c, *tank));
            }
            if (!entry)
                continue;   // over the search budget, never searched: next refresh
            f.predicted = true;
            f.joiners = entry->joiners;
            f.points = JoinPoints(world, entry->foes, (f.fleeing ? 0.0f : f.secToFlee) + JOIN_SEC);
        }
    }
}
