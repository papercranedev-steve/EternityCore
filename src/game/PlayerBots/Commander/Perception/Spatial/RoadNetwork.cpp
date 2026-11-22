#include "Commander/Perception/Spatial/RoadNetwork.h"
#include "Database/DatabaseEnv.h"
#include "GridDefines.h"
#include "GridMapDefines.h"
#include "Log.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "ObjectMgr.h"
#include "Detour/Include/DetourNavMeshQuery.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace Commander
{
namespace Roads
{
    namespace
    {
        // Detour search nodes for one road (the server's per-thread queries have 2048, about a
        // few hundred yards; a road is up to MAX_EDGE_YD). The pool's indexes are 16 bit.
        int const QUERY_NODES = 65000;
        int const MAX_POLYS = 8192;
        int const MAX_CORNERS = 2048;
        // findNearestPoly half extents (x, z, y): a node is a town's centre, maybe a roof.
        float const NODE_EXTENTS[3] = { 12.0f, 40.0f, 12.0f };
        float const HINT_EXTENTS[3] = { 6.0f, 12.0f, 6.0f };
        // A path whose end is this far (2d) from where it was asked to go did not get there.
        float const END_SLACK_YD = 8.0f;
        // Tiles loaded around the straight line for one road.
        float const TILE_MARGIN_YD = 250.0f;
        uint32 const MAX_TILES = 49;
        // Hints: within this of the shortest path (manual points: the wider one), not within a
        // node's core (+ margin: the place's own patrols), spaced along the road.
        float const HINT_CORRIDOR_YD = 50.0f;
        float const MANUAL_CORRIDOR_YD = 120.0f;
        float const CORE_MARGIN_YD = 30.0f;
        float const HINT_SPACING_YD = 60.0f;
        size_t const MAX_HINTS = 40;
        // The road through the hints may be this much longer than the shortest one (another
        // road's patrol pulled it away).
        float const MAX_HINT_DETOUR = 1.5f;
        // Douglas-Peucker tolerance (2d).
        float const SIMPLIFY_YD = 3.0f;
        // Detour cost of a yard of open water (as PathInfo for bots).
        float const SWIM_COST = 3.0f;
        uint32 const ALLIANCE_PLAYER_FACTION = 1;
        uint32 const HORDE_PLAYER_FACTION = 2;

        float Dist2d(float ax, float ay, float bx, float by)
        {
            return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
        }

        // Distance (2d) from p to the segment ab, and the segment parameter t of the nearest point.
        float SegmentDist(float px, float py, RoadPoint const& a, RoadPoint const& b, float& t)
        {
            float const dx = b.x - a.x, dy = b.y - a.y;
            float const len2 = dx * dx + dy * dy;
            t = len2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy) / len2 : 0.0f;
            t = std::min(1.0f, std::max(0.0f, t));
            return Dist2d(px, py, a.x + dx * t, a.y + dy * t);
        }

        void Simplify(std::vector<RoadPoint> const& in, size_t first, size_t last, std::vector<bool>& keep)
        {
            if (last <= first + 1)
                return;
            float worst = 0.0f;
            size_t index = first;
            for (size_t i = first + 1; i < last; ++i)
            {
                float t;
                float const d = SegmentDist(in[i].x, in[i].y, in[first], in[last], t);
                if (d > worst)
                {
                    worst = d;
                    index = i;
                }
            }
            if (worst <= SIMPLIFY_YD)
                return;
            keep[index] = true;
            Simplify(in, first, index, keep);
            Simplify(in, index, last, keep);
        }

        std::vector<RoadPoint> DouglasPeucker(std::vector<RoadPoint> const& in)
        {
            if (in.size() < 3)
                return in;
            std::vector<bool> keep(in.size(), false);
            keep.front() = keep.back() = true;
            Simplify(in, 0, in.size() - 1, keep);
            std::vector<RoadPoint> out;
            for (size_t i = 0; i < in.size(); ++i)
                if (keep[i])
                    out.push_back(in[i]);
            return out;
        }

        float LineLength(std::vector<RoadPoint> const& line)
        {
            float length = 0.0f;
            for (size_t i = 1; i < line.size(); ++i)
                length += Dist2d(line[i - 1].x, line[i - 1].y, line[i].x, line[i].y);
            return length;
        }

        // Grid indexes as TerrainInfo::LoadMapAndVMap gives them to MMapManager::loadMap.
        int32 GridX(float worldY) { return int32(32.0f - worldY / SIZE_OF_GRIDS); }
        int32 GridY(float worldX) { return int32(32.0f - worldX / SIZE_OF_GRIDS); }

        // The navmesh tiles under the box: loads those not loaded and unloads them again when it
        // goes out of scope (only those it loaded: the maps' own grids keep theirs).
        class TileScope
        {
        public:
            TileScope(uint32 mapId, float minX, float minY, float maxX, float maxY) : m_mapId(mapId)
            {
                int32 const x0 = std::max(0, GridX(maxY)), x1 = std::min(MAX_NUMBER_OF_GRIDS - 1, GridX(minY));
                int32 const y0 = std::max(0, GridY(maxX)), y1 = std::min(MAX_NUMBER_OF_GRIDS - 1, GridY(minX));
                if (uint32((x1 - x0 + 1) * (y1 - y0 + 1)) > MAX_TILES)
                {
                    m_tooBig = true;
                    return;
                }
                MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
                for (int32 x = x0; x <= x1; ++x)
                    for (int32 y = y0; y <= y1; ++y)
                        if (mmap->loadMap(mapId, x, y))
                            m_loaded.emplace_back(x, y);
            }
            ~TileScope()
            {
                MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
                for (auto const& t : m_loaded)
                    mmap->unloadMap(m_mapId, t.first, t.second);
            }
            bool TooBig() const { return m_tooBig; }

        private:
            uint32 m_mapId;
            bool m_tooBig = false;
            std::vector<std::pair<int32, int32>> m_loaded;
        };

        dtQueryFilter RoadFilter()
        {
            dtQueryFilter filter;
            filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
            filter.setExcludeFlags(NAV_STEEP_SLOPES);
            filter.setAreaCost(AREA_WATER, SWIM_COST);
            return filter;
        }
    }

    RoadNetwork::~RoadNetwork()
    {
        for (auto const& q : m_queries)
            dtFreeNavMeshQuery(q.second);
    }

    void RoadNetwork::Begin(std::vector<RoadNode> nodes, std::vector<RoadHint> hints)
    {
        m_nodes = std::move(nodes);
        m_hints = std::move(hints);
        m_edges.clear();
        m_planned.clear();
        m_next = 0;
        m_failed = 0;
        m_buildMs = 0;
        PlanEdges();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Roads: %u nodes, %u hint points, %u roads to build.",
            uint32(m_nodes.size()), uint32(m_hints.size()), uint32(m_planned.size()));
    }

    void RoadNetwork::PlanEdges()
    {
        // Gabriel graph per map: no third node inside the circle on ab.
        for (uint32 a = 0; a < m_nodes.size(); ++a)
        {
            for (uint32 b = a + 1; b < m_nodes.size(); ++b)
            {
                RoadNode const& na = m_nodes[a];
                RoadNode const& nb = m_nodes[b];
                if (na.mapId != nb.mapId)
                    continue;
                float const d = Dist2d(na.x, na.y, nb.x, nb.y);
                if (d > MAX_EDGE_YD || d < 1.0f)
                    continue;
                float const mx = (na.x + nb.x) * 0.5f, my = (na.y + nb.y) * 0.5f;
                float const r2 = d * d * 0.25f;
                bool blocked = false;
                for (uint32 c = 0; c < m_nodes.size() && !blocked; ++c)
                {
                    RoadNode const& nc = m_nodes[c];
                    if (c == a || c == b || nc.mapId != na.mapId)
                        continue;
                    blocked = (nc.x - mx) * (nc.x - mx) + (nc.y - my) * (nc.y - my) < r2;
                }
                if (!blocked)
                    m_planned.push_back({ a, b });
            }
        }
    }

    dtNavMeshQuery* RoadNetwork::QueryFor(uint32 mapId)
    {
        auto it = m_queries.find(mapId);
        if (it != m_queries.end())
            return it->second;
        dtNavMesh const* mesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapId);
        if (!mesh)
            return nullptr;
        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        if (!query || dtStatusFailed(query->init(mesh, QUERY_NODES)))
        {
            if (query)
                dtFreeNavMeshQuery(query);
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] Roads: no navmesh query for map %u.", mapId);
            return nullptr;
        }
        m_queries[mapId] = query;
        return query;
    }

    bool RoadNetwork::FindPath(dtNavMeshQuery* query, RoadPoint const& from, RoadPoint const& to, std::vector<RoadPoint>& out) const
    {
        dtQueryFilter const filter = RoadFilter();
        // Detour: (y, z, x).
        float const start[3] = { from.y, from.z, from.x };
        float const end[3] = { to.y, to.z, to.x };
        float startPt[3], endPt[3];
        dtPolyRef startRef = 0, endRef = 0;
        if (dtStatusFailed(query->findNearestPoly(start, NODE_EXTENTS, &filter, &startRef, startPt)) || !startRef ||
            dtStatusFailed(query->findNearestPoly(end, NODE_EXTENTS, &filter, &endRef, endPt)) || !endRef)
            return false;

        static thread_local std::vector<dtPolyRef> polys(MAX_POLYS);
        int polyCount = 0;
        if (dtStatusFailed(query->findPath(startRef, endRef, startPt, endPt, &filter, polys.data(), &polyCount, MAX_POLYS)) ||
            polyCount <= 0 || polys[polyCount - 1] != endRef)
            return false;

        static thread_local std::vector<float> corners(MAX_CORNERS * 3);
        int cornerCount = 0;
        if (dtStatusFailed(query->findStraightPath(startPt, endPt, polys.data(), polyCount, corners.data(), nullptr, nullptr,
                &cornerCount, MAX_CORNERS)) || cornerCount < 2)
            return false;

        RoadPoint const last = { corners[(cornerCount - 1) * 3 + 2], corners[(cornerCount - 1) * 3], corners[(cornerCount - 1) * 3 + 1] };
        if (Dist2d(last.x, last.y, endPt[2], endPt[0]) > END_SLACK_YD)
            return false;   // ran out of corners

        for (int i = 0; i < cornerCount; ++i)
        {
            RoadPoint p = { corners[i * 3 + 2], corners[i * 3], corners[i * 3 + 1] };
            if (!out.empty() && Dist2d(out.back().x, out.back().y, p.x, p.y) < 0.5f)
                continue;
            out.push_back(p);
        }
        return out.size() >= 2;
    }

    bool RoadNetwork::Shape(dtNavMeshQuery* query, RoadNode const& a, RoadNode const& b, RoadEdge& out) const
    {
        RoadPoint const from = { a.x, a.y, a.z };
        RoadPoint const to = { b.x, b.y, b.z };
        std::vector<RoadPoint> shortest;
        if (!FindPath(query, from, to, shortest))
            return false;
        float const shortestLength = LineLength(shortest);

        // Hints near the shortest way, not the places' own, in order along it.
        RoadEdge probe;
        probe.points = shortest;
        probe.along.assign(shortest.size(), 0.0f);
        for (size_t i = 1; i < shortest.size(); ++i)
            probe.along[i] = probe.along[i - 1] + Dist2d(shortest[i - 1].x, shortest[i - 1].y, shortest[i].x, shortest[i].y);
        probe.length = shortestLength;

        std::vector<std::pair<float, RoadHint const*>> nearHints;
        float const minX = std::min(a.x, b.x) - MANUAL_CORRIDOR_YD - TILE_MARGIN_YD, maxX = std::max(a.x, b.x) + MANUAL_CORRIDOR_YD + TILE_MARGIN_YD;
        float const minY = std::min(a.y, b.y) - MANUAL_CORRIDOR_YD - TILE_MARGIN_YD, maxY = std::max(a.y, b.y) + MANUAL_CORRIDOR_YD + TILE_MARGIN_YD;
        for (RoadHint const& h : m_hints)
        {
            if (h.mapId != a.mapId || h.x < minX || h.x > maxX || h.y < minY || h.y > maxY)
                continue;
            float dist;
            float const s = Project(probe, h.x, h.y, dist);
            if (dist > (h.manual ? MANUAL_CORRIDOR_YD : HINT_CORRIDOR_YD))
                continue;
            if (Dist2d(h.x, h.y, a.x, a.y) < a.coreYd + CORE_MARGIN_YD || Dist2d(h.x, h.y, b.x, b.y) < b.coreYd + CORE_MARGIN_YD)
                continue;
            bool inOtherCore = false;
            for (RoadNode const& n : m_nodes)
                if (n.mapId == h.mapId && Dist2d(h.x, h.y, n.x, n.y) < n.coreYd + CORE_MARGIN_YD)
                {
                    inOtherCore = true;
                    break;
                }
            if (!inOtherCore)
                nearHints.emplace_back(s, &h);
        }
        std::sort(nearHints.begin(), nearHints.end(), [](auto const& l, auto const& r) { return l.first < r.first; });

        std::vector<RoadHint const*> via;
        float lastS = HINT_SPACING_YD * 0.5f;
        for (auto const& n : nearHints)
        {
            if (n.first - lastS < HINT_SPACING_YD && !n.second->manual)
                continue;
            if (n.first > shortestLength - HINT_SPACING_YD * 0.5f)
                break;
            via.push_back(n.second);
            lastS = n.first;
            if (via.size() >= MAX_HINTS)
                break;
        }

        std::vector<RoadPoint> line = shortest;
        if (!via.empty())
        {
            // Through the hints; a hint off the mesh is skipped, a leg that fails gives up the hints.
            dtQueryFilter const filter = RoadFilter();
            std::vector<RoadPoint> through;
            RoadPoint leg = from;
            bool ok = true;
            uint32 used = 0;
            for (RoadHint const* h : via)
            {
                float const p[3] = { h->y, h->z, h->x };
                float onMesh[3];
                dtPolyRef ref = 0;
                if (dtStatusFailed(query->findNearestPoly(p, HINT_EXTENTS, &filter, &ref, onMesh)) || !ref)
                    continue;
                RoadPoint const next = { onMesh[2], onMesh[0], onMesh[1] };
                std::vector<RoadPoint> part;
                if (!FindPath(query, leg, next, part))
                {
                    ok = false;
                    break;
                }
                if (!through.empty())
                    part.erase(part.begin());
                through.insert(through.end(), part.begin(), part.end());
                leg = next;
                ++used;
            }
            std::vector<RoadPoint> tail;
            if (ok && FindPath(query, leg, to, tail))
            {
                if (!through.empty())
                    tail.erase(tail.begin());
                through.insert(through.end(), tail.begin(), tail.end());
                if (through.size() >= 2 && LineLength(through) <= shortestLength * MAX_HINT_DETOUR)
                {
                    line = std::move(through);
                    out.hints = used;
                }
            }
        }

        out.points = DouglasPeucker(line);
        out.along.assign(out.points.size(), 0.0f);
        out.minX = out.maxX = out.points[0].x;
        out.minY = out.maxY = out.points[0].y;
        for (size_t i = 1; i < out.points.size(); ++i)
        {
            RoadPoint const& p = out.points[i];
            out.along[i] = out.along[i - 1] + Dist2d(out.points[i - 1].x, out.points[i - 1].y, p.x, p.y);
            out.minX = std::min(out.minX, p.x);
            out.maxX = std::max(out.maxX, p.x);
            out.minY = std::min(out.minY, p.y);
            out.maxY = std::max(out.maxY, p.y);
        }
        out.length = out.along.back();
        return true;
    }

    bool RoadNetwork::Step()
    {
        if (m_next >= m_planned.size())
            return false;
        auto const started = std::chrono::steady_clock::now();
        Plan const plan = m_planned[m_next++];
        RoadNode const& a = m_nodes[plan.a];
        RoadNode const& b = m_nodes[plan.b];

        bool built = false;
        {
            TileScope tiles(a.mapId, std::min(a.x, b.x) - TILE_MARGIN_YD, std::min(a.y, b.y) - TILE_MARGIN_YD,
                std::max(a.x, b.x) + TILE_MARGIN_YD, std::max(a.y, b.y) + TILE_MARGIN_YD);
            if (!tiles.TooBig())
            {
                if (dtNavMeshQuery* query = QueryFor(a.mapId))
                {
                    RoadEdge edge;
                    if (Shape(query, a, b, edge))
                    {
                        edge.id = uint32(m_edges.size() + 1);
                        edge.mapId = a.mapId;
                        edge.nodeA = a.id;
                        edge.nodeB = b.id;
                        m_edges.push_back(std::move(edge));
                        built = true;
                    }
                }
            }
        }
        if (!built)
            ++m_failed;

        m_buildMs += uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        if (m_next >= m_planned.size())
        {
            uint32 hinted = 0;
            for (RoadEdge const& e : m_edges)
                hinted += e.hints ? 1 : 0;
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Roads built: %u of %u (%u through patrol or road points, %u failed), %u ms.",
                uint32(m_edges.size()), uint32(m_planned.size()), hinted, m_failed, m_buildMs);
        }
        return m_next < m_planned.size();
    }

    RoadNode const* RoadNetwork::FindNode(uint32 id) const
    {
        for (RoadNode const& n : m_nodes)
            if (n.id == id)
                return &n;
        return nullptr;
    }

    std::vector<RoadEdge const*> RoadNetwork::Near(uint32 mapId, float x, float y, float radius) const
    {
        std::vector<RoadEdge const*> out;
        for (RoadEdge const& e : m_edges)
        {
            if (e.mapId != mapId || x < e.minX - radius || x > e.maxX + radius || y < e.minY - radius || y > e.maxY + radius)
                continue;
            float dist;
            Project(e, x, y, dist);
            if (dist <= radius)
                out.push_back(&e);
        }
        return out;
    }

    float RoadNetwork::Project(RoadEdge const& edge, float x, float y, float& dist)
    {
        dist = 1e9f;
        float best = 0.0f;
        for (size_t i = 1; i < edge.points.size(); ++i)
        {
            float t;
            float const d = SegmentDist(x, y, edge.points[i - 1], edge.points[i], t);
            if (d < dist)
            {
                dist = d;
                best = edge.along[i - 1] + t * (edge.along[i] - edge.along[i - 1]);
            }
        }
        return best;
    }

    RoadPoint RoadNetwork::PointAt(RoadEdge const& edge, float s)
    {
        if (edge.points.empty())
            return RoadPoint();
        if (s <= 0.0f)
            return edge.points.front();
        for (size_t i = 1; i < edge.points.size(); ++i)
        {
            if (s <= edge.along[i])
            {
                float const span = edge.along[i] - edge.along[i - 1];
                float const t = span > 0.0f ? (s - edge.along[i - 1]) / span : 0.0f;
                RoadPoint const& a = edge.points[i - 1];
                RoadPoint const& b = edge.points[i];
                return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
            }
        }
        return edge.points.back();
    }

    std::vector<RoadHint> RoadNetwork::LoadPatrolHints()
    {
        std::vector<RoadHint> hints;
        FactionTemplateEntry const* ally = sObjectMgr.GetFactionTemplateEntry(ALLIANCE_PLAYER_FACTION);
        FactionTemplateEntry const* horde = sObjectMgr.GetFactionTemplateEntry(HORDE_PLAYER_FACTION);
        if (!ally || !horde)
            return hints;

        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT `id`, `position_x`, `position_y`, `position_z` FROM `creature_movement` ORDER BY `id`, `point`"));
        if (!result)
            return hints;

        uint32 lastGuid = 0;
        bool keep = false;
        uint32 mapId = 0;
        do
        {
            Field* f = result->Fetch();
            uint32 const guid = f[0].GetUInt32();
            if (guid != lastGuid)
            {
                lastGuid = guid;
                keep = false;
                CreatureData const* data = sObjectMgr.GetCreatureData(guid);
                if (!data || (data->position.mapId != 0 && data->position.mapId != 1) || !data->creature_id[0])
                    continue;
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(data->creature_id[0]);
                FactionTemplateEntry const* ft = info ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
                // Someone a player of one side walks past in peace (guards, couriers, travellers).
                keep = ft && (!ft->IsHostileTo(*ally) || !ft->IsHostileTo(*horde));
                mapId = data->position.mapId;
            }
            if (!keep)
                continue;
            RoadHint h;
            h.mapId = mapId;
            h.x = f[1].GetFloat();
            h.y = f[2].GetFloat();
            h.z = f[3].GetFloat();
            hints.push_back(h);
        } while (result->NextRow());
        return hints;
    }
}
}
