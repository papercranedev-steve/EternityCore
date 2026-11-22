/*
 * Roads between the places of a continent (ambient flow doc .kiro/docs/2026-11-22-ambient-flow.md §3.1).
 *
 * The world data has no roads. They are made from what is there:
 * - which places are linked: a Gabriel graph of the nodes of each map (a and b are linked when
 *   no third node lies in the circle whose diameter is ab), edges up to a length cap;
 * - the shape: the Detour shortest path between the two, then once more through the patrol
 *   waypoints near it (guards and couriers walk the roads; creature_movement) and the manual
 *   road points, simplified with Douglas-Peucker.
 *
 * Layer 1 (eyes): a fact about the world, no choice. The nodes come from the caller (Population's
 * settlements), so this file knows no population code.
 *
 * Threads: Begin / Step on the world thread while the map threads are idle (PopulationMgr::Update):
 * Step loads the navmesh tiles an edge needs that are not loaded, finds the path and unloads them
 * again in the same call (NavPather). Queries are
 * read-only, also from map threads once built (the edges are not changed until the next Begin,
 * which the caller only does from the world thread).
 */

#ifndef MANGOS_COMMANDER_ROAD_NETWORK_H
#define MANGOS_COMMANDER_ROAD_NETWORK_H

#include "Common.h"
#include "Commander/Perception/Spatial/NavPather.h"
#include <string>
#include <vector>

namespace Commander
{
    namespace Roads
    {
        // A place roads lead to (a settlement).
        struct RoadNode
        {
            uint32 id = 0;          // the caller's id (Settlements id)
            uint32 mapId = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float coreYd = 0.0f;    // patrol points this close (+ margin) are the place's own, not a road's
            std::string name;       // for the log
        };

        // A planned road that could not be built.
        struct FailedRoad
        {
            uint32 mapId = 0;
            uint32 nodeA = 0, nodeB = 0;        // RoadNode ids
            float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
            PathFail why = PathFail::None;
        };

        // A point some road passes (patrol waypoint, manual road point).
        struct RoadHint
        {
            uint32 mapId = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            bool manual = false;    // recorded by hand: a wider corridor
        };

        struct RoadEdge
        {
            uint32 id = 0;
            uint32 mapId = 0;
            uint32 nodeA = 0, nodeB = 0;        // RoadNode ids, points[0] is at A
            std::vector<RoadPoint> points;
            std::vector<float> along;           // 2d length from points[0] to each point
            float length = 0.0f;
            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
            uint32 hints = 0;                   // hint points the shape went through
        };

        class RoadNetwork
        {
        public:
            RoadNetwork() = default;
            RoadNetwork(RoadNetwork const&) = delete;
            RoadNetwork& operator=(RoadNetwork const&) = delete;

            // Forgets the roads and plans the edges between `nodes` (built by Step).
            void Begin(std::vector<RoadNode> nodes, std::vector<RoadHint> hints);
            // Builds the next planned edge. False when nothing is left.
            bool Step();
            bool IsBuilding() const { return m_next < m_planned.size(); }

            std::vector<RoadEdge> const& Edges() const { return m_edges; }
            std::vector<FailedRoad> const& Failures() const { return m_failures; }
            RoadNode const* FindNode(uint32 id) const;
            // Edges passing within `radius` (2d) of the point.
            std::vector<RoadEdge const*> Near(uint32 mapId, float x, float y, float radius) const;

            uint32 Planned() const { return uint32(m_planned.size()); }
            uint32 Done() const { return uint32(m_next); }
            uint32 Failed() const { return uint32(m_failures.size()); }
            uint32 HintCount() const { return uint32(m_hints.size()); }
            uint32 BuildMs() const { return m_buildMs; }

            // Patrol waypoints of creatures that are not hostile to both sides (guards, couriers),
            // on the continents. World data; call after creatures and waypoints are loaded.
            static std::vector<RoadHint> LoadPatrolHints();

            // Arc length along `edge` of the point nearest to (x, y); `dist`: its 2d distance.
            static float Project(RoadEdge const& edge, float x, float y, float& dist);
            // The point at arc length `s` (clamped), z interpolated.
            static RoadPoint PointAt(RoadEdge const& edge, float s);

            // Longest edge planned (yards, straight line). 2200 left out Goldshire - Lakeshire
            // (2186 between the inns, the edge of the cap) and many like it (user 2026-11-22: no
            // one on the main roads).
            static constexpr float MAX_EDGE_YD = 2700.0f;

        private:
            struct Plan
            {
                uint32 a = 0, b = 0;    // indexes into m_nodes
            };
            void PlanEdges();
            bool Shape(RoadNode const& a, RoadNode const& b, RoadEdge& out) const;

            std::vector<RoadNode> m_nodes;
            std::vector<RoadHint> m_hints;
            std::vector<Plan> m_planned;
            size_t m_next = 0;
            std::vector<RoadEdge> m_edges;
            NavPather m_pather;
            std::vector<FailedRoad> m_failures;
            uint32 m_buildMs = 0;
        };
    }
}

#endif
