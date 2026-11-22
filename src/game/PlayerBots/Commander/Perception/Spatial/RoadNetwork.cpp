#include "Commander/Perception/Spatial/RoadNetwork.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace Commander
{
namespace Roads
{
    namespace
    {
        // Tiles loaded around the straight line for one road.
        float const TILE_MARGIN_YD = 250.0f;
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

        void Measure(RoadEdge& e)
        {
            e.along.assign(e.points.size(), 0.0f);
            e.minX = e.maxX = e.points[0].x;
            e.minY = e.maxY = e.points[0].y;
            for (size_t i = 1; i < e.points.size(); ++i)
            {
                RoadPoint const& p = e.points[i];
                e.along[i] = e.along[i - 1] + Distance2d(e.points[i - 1], p);
                e.minX = std::min(e.minX, p.x);
                e.maxX = std::max(e.maxX, p.x);
                e.minY = std::min(e.minY, p.y);
                e.maxY = std::max(e.maxY, p.y);
            }
            e.length = e.along.back();
        }
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

    bool RoadNetwork::Shape(RoadNode const& a, RoadNode const& b, RoadEdge& out) const
    {
        RoadPoint const from = { a.x, a.y, a.z };
        RoadPoint const to = { b.x, b.y, b.z };
        std::vector<RoadPoint> shortest;
        if (!m_pather.Path(from, to, shortest))
            return false;
        float const shortestLength = LineLength(shortest);

        // Hints near the shortest way, not the places' own, in order along it.
        RoadEdge probe;
        probe.points = shortest;
        Measure(probe);

        std::vector<std::pair<float, RoadHint const*>> nearHints;
        float const margin = MANUAL_CORRIDOR_YD + TILE_MARGIN_YD;
        float const minX = std::min(a.x, b.x) - margin, maxX = std::max(a.x, b.x) + margin;
        float const minY = std::min(a.y, b.y) - margin, maxY = std::max(a.y, b.y) + margin;
        for (RoadHint const& h : m_hints)
        {
            if (h.mapId != a.mapId || h.x < minX || h.x > maxX || h.y < minY || h.y > maxY)
                continue;
            float dist;
            float const s = Project(probe, h.x, h.y, dist);
            if (dist > (h.manual ? MANUAL_CORRIDOR_YD : HINT_CORRIDOR_YD))
                continue;
            bool inCore = false;
            for (RoadNode const& n : m_nodes)
                if (n.mapId == h.mapId && Dist2d(h.x, h.y, n.x, n.y) < n.coreYd + CORE_MARGIN_YD)
                {
                    inCore = true;
                    break;
                }
            if (!inCore)
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
            std::vector<RoadPoint> through;
            RoadPoint leg = from;
            bool ok = true;
            uint32 used = 0;
            for (RoadHint const* h : via)
            {
                RoadPoint next;
                if (!m_pather.OnMesh({ h->x, h->y, h->z }, next))
                    continue;
                std::vector<RoadPoint> part;
                if (!m_pather.Path(leg, next, part))
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
            if (ok && m_pather.Path(leg, to, tail))
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

        out.points = Simplify(line, SIMPLIFY_YD);
        Measure(out);
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
        if (m_pather.BeginArea(a.mapId, std::min(a.x, b.x) - TILE_MARGIN_YD, std::min(a.y, b.y) - TILE_MARGIN_YD,
                std::max(a.x, b.x) + TILE_MARGIN_YD, std::max(a.y, b.y) + TILE_MARGIN_YD))
        {
            RoadEdge edge;
            if (Shape(a, b, edge))
            {
                edge.id = uint32(m_edges.size() + 1);
                edge.mapId = a.mapId;
                edge.nodeA = a.id;
                edge.nodeB = b.id;
                m_edges.push_back(std::move(edge));
                built = true;
            }
        }
        m_pather.EndArea();
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
