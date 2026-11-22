/*
 * Where the quests of the open world are done (ambient flow doc .kiro/docs/2026-11-22-ambient-flow.md
 * §4, P2): for each quest whose objective is killing a creature (or an item it drops), the
 * spawns of that creature clustered with DBSCAN (eps CLUSTER_EPS_YD, at least CLUSTER_MIN
 * spawns), the cluster nearest to who gives the quest, and the navmesh way from the giver to it.
 *
 * Only plain quests (type 0, no class or dungeon quests, no spell objectives), normal creatures
 * (no elites, civilians, guards, critters), givers that are creatures not hostile to a side, on
 * the continents. The population acts the quests out (user 2026-11-22: a pretend quest, real
 * kills, nothing saved): Population/FieldPopulation.
 *
 * Layer 1 (eyes): a fact about the world, no choice. Load at startup (world thread), Step builds
 * one way per call on the world thread while the map threads are idle (NavPather); read-only after.
 */

#ifndef MANGOS_COMMANDER_QUEST_AREAS_H
#define MANGOS_COMMANDER_QUEST_AREAS_H

#include "Common.h"
#include "Commander/Perception/Spatial/NavPather.h"
#include <vector>

namespace Commander
{
    namespace Quests
    {
        using Roads::RoadPoint;

        struct QuestArea
        {
            uint32 id = 0;                  // index + 1
            uint32 questId = 0;
            uint32 mapId = 0;
            RoadPoint center;               // the cluster's spawn nearest to its centroid
            float radiusYd = 0.0f;          // farthest spawn from the centre
            std::vector<RoadPoint> spawns;  // the creature's spawns in the cluster
            uint32 preyEntry = 0;
            uint32 need = 0;                // kills that make the quest (drops: a few more)
            uint8 preyLevelMin = 0, preyLevelMax = 0;
            uint8 questLevel = 0, minLevel = 0;
            bool alliance = false, horde = false;   // the giver is not hostile to this side
            uint32 giverEntry = 0;
            RoadPoint giver;                // its spawn nearest to the area
            // Giver -> area (navmesh, simplified); empty until built, or no way.
            std::vector<RoadPoint> way;
            std::vector<float> along;
            float wayLength = 0.0f;

            bool ServesTeam(uint32 team) const;
        };

        class QuestAreaIndex
        {
        public:
            // Reads the quests, loot and spawns (creatures and quests must be loaded).
            void Load();
            // Builds the next area's way. False when nothing is left.
            bool Step();
            bool IsLoaded() const { return m_loaded; }
            bool IsBuilding() const { return m_next < m_areas.size(); }

            std::vector<QuestArea> const& All() const { return m_areas; }
            QuestArea const* Find(uint32 id) const { return id && id <= m_areas.size() ? &m_areas[id - 1] : nullptr; }
            // Areas whose spawns come within `radius` (2d) of x/y.
            std::vector<QuestArea const*> Near(uint32 mapId, float x, float y, float radius) const;

            uint32 WaysBuilt() const { return m_ways; }
            uint32 BuildMs() const { return m_buildMs; }

            // Arc length on `area`'s way of the point nearest to (x, y); `dist`: its 2d distance.
            static float Project(QuestArea const& area, float x, float y, float& dist);
            static RoadPoint PointAt(QuestArea const& area, float s);

            static constexpr float CLUSTER_EPS_YD = 45.0f;
            static constexpr uint32 CLUSTER_MIN = 3;
            // A cluster farther than this from every spawn of the giver is not the quest's.
            static constexpr float MAX_GIVER_YD = 2000.0f;

        private:
            std::vector<QuestArea> m_areas;
            size_t m_next = 0;
            Roads::NavPather m_pather;
            bool m_loaded = false;
            uint32 m_ways = 0;
            uint32 m_buildMs = 0;
        };
    }
}

#endif
