/*
 * Long navmesh paths (hundreds to a few thousand yards) computed away from any unit, for
 * world facts built at startup: the roads (RoadNetwork) and the ways from quest givers to their
 * quests' creatures (QuestAreas). The server's PathInfo needs a unit, reaches a few hundred
 * yards (2048 search nodes, 256 polygons) and only the tiles loaded around players.
 *
 * BeginArea loads the navmesh tiles under a box that are not loaded (Terrain.Preload.Continents
 * is off on the live server), EndArea unloads exactly those again. Both and the queries in
 * between belong in one call on the world thread while the map threads are idle
 * (PopulationMgr::Update): tiles are added to and removed from the map's navmesh.
 */

#ifndef MANGOS_COMMANDER_NAV_PATHER_H
#define MANGOS_COMMANDER_NAV_PATHER_H

#include "Common.h"
#include <map>
#include <utility>
#include <vector>

class dtNavMeshQuery;

namespace Commander
{
    namespace Roads
    {
        struct RoadPoint
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Why the last BeginArea / Path did not work (logged by the callers).
        enum class PathFail : uint8
        {
            None,
            TooManyTiles,   // BeginArea: the box needs more than MAX_TILES
            NoNavMesh,      // BeginArea: no navmesh for the map
            NoStart,        // `from` is not near the mesh
            NoEnd,          // `to` is not near the mesh
            NoRoute,        // no way between them (another island, or a search that stops gaining)
            TooLong,        // still not there after MAX_LEGS searches
        };
        char const* PathFailName(PathFail fail);

        class NavPather
        {
        public:
            NavPather() = default;
            ~NavPather();
            NavPather(NavPather const&) = delete;
            NavPather& operator=(NavPather const&) = delete;

            // False: too many tiles (MAX_TILES) or no navmesh for the map; EndArea anyway.
            bool BeginArea(uint32 mapId, float minX, float minY, float maxX, float maxY);
            void EndArea();

            // The navmesh corners from `from` to `to` (both snapped to the mesh), complete only.
            // A search that runs out of nodes, polygons or corners goes on from where it got to
            // (up to MAX_LEGS searches, each one closer by MIN_LEG_GAIN_YD at least).
            bool Path(RoadPoint const& from, RoadPoint const& to, std::vector<RoadPoint>& out) const;
            // The mesh point nearest to `p` within a few yards.
            bool OnMesh(RoadPoint const& p, RoadPoint& out) const;
            PathFail LastFail() const { return m_lastFail; }

            static constexpr uint32 MAX_TILES = 49;
            static constexpr uint32 MAX_LEGS = 8;
            static constexpr float MIN_LEG_GAIN_YD = 10.0f;

        private:
            dtNavMeshQuery* QueryFor(uint32 mapId);

            mutable PathFail m_lastFail = PathFail::None;
            uint32 m_mapId = 0;
            dtNavMeshQuery* m_query = nullptr;     // of m_mapId while an area is begun
            std::vector<std::pair<int32, int32>> m_loaded;
            std::map<uint32, dtNavMeshQuery*> m_queries;
        };

        // Small helpers for lines of points (2d).
        float Distance2d(RoadPoint const& a, RoadPoint const& b);
        float LineLength(std::vector<RoadPoint> const& line);
        // Douglas-Peucker with `toleranceYd`.
        std::vector<RoadPoint> Simplify(std::vector<RoadPoint> const& line, float toleranceYd);
    }
}

#endif
