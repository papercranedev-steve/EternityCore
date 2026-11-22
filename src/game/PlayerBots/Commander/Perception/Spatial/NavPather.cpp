#include "Commander/Perception/Spatial/NavPather.h"
#include "GridDefines.h"
#include "GridMapDefines.h"
#include "Log.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "Detour/Include/DetourNavMeshQuery.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace Roads
{
    namespace
    {
        // Detour search nodes for one path (the pool's indexes are 16 bit).
        int const QUERY_NODES = 65000;
        int const MAX_POLYS = 8192;
        int const MAX_CORNERS = 2048;
        // findNearestPoly half extents (x, z, y): an end may be a town's centre, maybe a roof.
        float const END_EXTENTS[3] = { 12.0f, 40.0f, 12.0f };
        float const POINT_EXTENTS[3] = { 6.0f, 12.0f, 6.0f };
        // A path whose end is this far (2d) from where it was asked to go did not get there.
        float const END_SLACK_YD = 8.0f;
        // Detour cost of a yard of open water (as PathInfo for bots).
        float const SWIM_COST = 3.0f;

        // Grid indexes as TerrainInfo::LoadMapAndVMap gives them to MMapManager::loadMap.
        int32 GridX(float worldY) { return int32(32.0f - worldY / SIZE_OF_GRIDS); }
        int32 GridY(float worldX) { return int32(32.0f - worldX / SIZE_OF_GRIDS); }

        dtQueryFilter WalkFilter()
        {
            dtQueryFilter filter;
            filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
            filter.setExcludeFlags(NAV_STEEP_SLOPES);
            filter.setAreaCost(AREA_WATER, SWIM_COST);
            return filter;
        }

        float SegmentDist(RoadPoint const& p, RoadPoint const& a, RoadPoint const& b)
        {
            float const dx = b.x - a.x, dy = b.y - a.y;
            float const len2 = dx * dx + dy * dy;
            float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
            t = std::min(1.0f, std::max(0.0f, t));
            float const ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
            return std::sqrt(ex * ex + ey * ey);
        }

        void SimplifyRange(std::vector<RoadPoint> const& in, size_t first, size_t last, float tolerance, std::vector<bool>& keep)
        {
            if (last <= first + 1)
                return;
            float worst = 0.0f;
            size_t index = first;
            for (size_t i = first + 1; i < last; ++i)
            {
                float const d = SegmentDist(in[i], in[first], in[last]);
                if (d > worst)
                {
                    worst = d;
                    index = i;
                }
            }
            if (worst <= tolerance)
                return;
            keep[index] = true;
            SimplifyRange(in, first, index, tolerance, keep);
            SimplifyRange(in, index, last, tolerance, keep);
        }
    }

    float Distance2d(RoadPoint const& a, RoadPoint const& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    }

    float LineLength(std::vector<RoadPoint> const& line)
    {
        float length = 0.0f;
        for (size_t i = 1; i < line.size(); ++i)
            length += Distance2d(line[i - 1], line[i]);
        return length;
    }

    std::vector<RoadPoint> Simplify(std::vector<RoadPoint> const& line, float toleranceYd)
    {
        if (line.size() < 3)
            return line;
        std::vector<bool> keep(line.size(), false);
        keep.front() = keep.back() = true;
        SimplifyRange(line, 0, line.size() - 1, toleranceYd, keep);
        std::vector<RoadPoint> out;
        for (size_t i = 0; i < line.size(); ++i)
            if (keep[i])
                out.push_back(line[i]);
        return out;
    }

    NavPather::~NavPather()
    {
        EndArea();
        for (auto const& q : m_queries)
            dtFreeNavMeshQuery(q.second);
    }

    dtNavMeshQuery* NavPather::QueryFor(uint32 mapId)
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
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] NavPather: no navmesh query for map %u.", mapId);
            return nullptr;
        }
        m_queries[mapId] = query;
        return query;
    }

    bool NavPather::BeginArea(uint32 mapId, float minX, float minY, float maxX, float maxY)
    {
        EndArea();
        m_mapId = mapId;
        int32 const x0 = std::max(0, GridX(maxY)), x1 = std::min(MAX_NUMBER_OF_GRIDS - 1, GridX(minY));
        int32 const y0 = std::max(0, GridY(maxX)), y1 = std::min(MAX_NUMBER_OF_GRIDS - 1, GridY(minX));
        if (x1 < x0 || y1 < y0 || uint32((x1 - x0 + 1) * (y1 - y0 + 1)) > MAX_TILES)
            return false;
        MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
        for (int32 x = x0; x <= x1; ++x)
            for (int32 y = y0; y <= y1; ++y)
                if (mmap->loadMap(mapId, x, y))
                    m_loaded.emplace_back(x, y);
        m_query = QueryFor(mapId);
        return m_query != nullptr;
    }

    void NavPather::EndArea()
    {
        if (!m_loaded.empty())
        {
            MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
            for (auto const& t : m_loaded)
                mmap->unloadMap(m_mapId, t.first, t.second);
            m_loaded.clear();
        }
        m_query = nullptr;
    }

    bool NavPather::OnMesh(RoadPoint const& p, RoadPoint& out) const
    {
        if (!m_query)
            return false;
        dtQueryFilter const filter = WalkFilter();
        float const pos[3] = { p.y, p.z, p.x };
        float onMesh[3];
        dtPolyRef ref = 0;
        if (dtStatusFailed(m_query->findNearestPoly(pos, POINT_EXTENTS, &filter, &ref, onMesh)) || !ref)
            return false;
        out = { onMesh[2], onMesh[0], onMesh[1] };
        return true;
    }

    bool NavPather::Path(RoadPoint const& from, RoadPoint const& to, std::vector<RoadPoint>& out) const
    {
        if (!m_query)
            return false;
        dtQueryFilter const filter = WalkFilter();
        // Detour: (y, z, x).
        float const start[3] = { from.y, from.z, from.x };
        float const end[3] = { to.y, to.z, to.x };
        float startPt[3], endPt[3];
        dtPolyRef startRef = 0, endRef = 0;
        if (dtStatusFailed(m_query->findNearestPoly(start, END_EXTENTS, &filter, &startRef, startPt)) || !startRef ||
            dtStatusFailed(m_query->findNearestPoly(end, END_EXTENTS, &filter, &endRef, endPt)) || !endRef)
            return false;

        static thread_local std::vector<dtPolyRef> polys(MAX_POLYS);
        int polyCount = 0;
        if (dtStatusFailed(m_query->findPath(startRef, endRef, startPt, endPt, &filter, polys.data(), &polyCount, MAX_POLYS)) ||
            polyCount <= 0 || polys[polyCount - 1] != endRef)
            return false;

        static thread_local std::vector<float> corners(MAX_CORNERS * 3);
        int cornerCount = 0;
        if (dtStatusFailed(m_query->findStraightPath(startPt, endPt, polys.data(), polyCount, corners.data(), nullptr, nullptr,
                &cornerCount, MAX_CORNERS)) || cornerCount < 2)
            return false;

        RoadPoint const last = { corners[(cornerCount - 1) * 3 + 2], corners[(cornerCount - 1) * 3], corners[(cornerCount - 1) * 3 + 1] };
        RoadPoint const wanted = { endPt[2], endPt[0], endPt[1] };
        if (Distance2d(last, wanted) > END_SLACK_YD)
            return false;   // ran out of corners

        for (int i = 0; i < cornerCount; ++i)
        {
            RoadPoint const p = { corners[i * 3 + 2], corners[i * 3], corners[i * 3 + 1] };
            if (!out.empty() && Distance2d(out.back(), p) < 0.5f)
                continue;
            out.push_back(p);
        }
        return out.size() >= 2;
    }
}
}
