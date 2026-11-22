#include "Population/TravellerPopulation.h"
#include "Population/CityPopulation.h"
#include "Population/FieldPopulation.h"
#include "Population/Observers.h"
#include "Population/PopulationMgr.h"
#include "Population/ScenePoints.h"
#include "Population/Settlements.h"
#include "Population/TownPopulation.h"
#include "Config/Config.h"
#include "GridMap.h"
#include "Map.h"
#include "MapManager.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace Population
{
    using Commander::Roads::RoadEdge;
    using Commander::Roads::RoadHint;
    using Commander::Roads::RoadNetwork;
    using Commander::Roads::RoadNode;
    using Commander::Roads::RoadPoint;

    namespace
    {
        uint32 const TICK_MS = 2 * IN_MILLISECONDS;
        // A traveller walks past an observer's near stretch of road at about this speed (yd/s):
        // how many are on it already when he logs in (rate x the time to walk it).
        float const PASS_SPEED = 7.0f;
        // Ahead of a walking observer this often (they come towards him).
        uint32 const AHEAD_PCT = 75;
        // Searched along the road from the observer's place on it.
        float const SEARCH_FIRST_YD = 40.0f;
        float const SEARCH_LAST_YD = 600.0f;
        float const SEARCH_STEP_YD = 10.0f;
        uint32 const PLACE_ANYWHERE_TRIES = 12;

        bool IsCityZone(uint32 zone)
        {
            for (CityDef const& def : CityDirector::Cities())
                if (def.zone == zone)
                    return true;
            return false;
        }

        uint32 ClampU(int32 value, uint32 lo, uint32 hi)
        {
            return uint32(std::min<int32>(std::max<int32>(value, int32(lo)), int32(hi)));
        }

        // Gravity size of a settlement kind.
        float SizeOf(SettlementKind kind)
        {
            switch (kind)
            {
                case SettlementKind::Capital:   return 3.0f;
                case SettlementKind::StartArea: return 1.0f;
                case SettlementKind::Town:      return 1.0f;
                case SettlementKind::Camp:      return 0.5f;
                case SettlementKind::Manual:    return 0.7f;
            }
            return 0.5f;
        }

        // Knuth's sampler (small means only).
        uint32 Poisson(float mean)
        {
            if (mean <= 0.0f)
                return 0;
            float const limit = std::exp(-std::min(mean, 20.0f));
            float p = 1.0f;
            uint32 k = 0;
            do
            {
                ++k;
                p *= frand(0.0f, 1.0f);
            } while (p > limit && k < 50);
            return k - 1;
        }

        float Dist2d(float ax, float ay, float bx, float by)
        {
            return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
        }

        RoadEdge const* PickByRate(std::vector<RoadEdge const*> const& roads, TravellerDirector const& director)
        {
            float total = 0.0f;
            for (RoadEdge const* e : roads)
                total += director.PerHour(*e);
            float roll = frand(0.0f, total);
            for (RoadEdge const* e : roads)
            {
                roll -= director.PerHour(*e);
                if (roll <= 0.0f)
                    return e;
            }
            return roads.empty() ? nullptr : roads.back();
        }
    }

    void TravellerDirector::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Road.Enable", true);
        m_perHour = float(ClampU(sConfig.GetIntDefault("PopBot.Road.PerHour", 20), 0, 600));
        m_maxNear = ClampU(sConfig.GetIntDefault("PopBot.Road.MaxNear", 4), 0, 20);
        m_mountPct = ClampU(sConfig.GetIntDefault("PopBot.Road.MountPct", 60), 0, 100);
    }

    void TravellerDirector::BeginRoads(Settlements const& settlements, CityDirector const& cities, ScenePoints const& points)
    {
        if (!m_patrolLoaded)
        {
            m_patrolHints = RoadNetwork::LoadPatrolHints();
            m_patrolLoaded = true;
        }
        std::vector<RoadHint> hints = m_patrolHints;
        for (ScenePoint const& p : points.All())
        {
            if (p.type != "road" || (p.mapId != 0 && p.mapId != 1))
                continue;
            RoadHint h;
            h.mapId = p.mapId;
            h.x = p.x;
            h.y = p.y;
            h.z = p.z;
            h.manual = true;
            hints.push_back(h);
        }

        std::vector<RoadNode> nodes;
        m_nodeSize.clear();
        for (Settlement const& s : settlements.All())
        {
            RoadNode n;
            n.id = s.id;
            n.mapId = s.mapId;
            n.x = s.x;
            n.y = s.y;
            n.z = s.z;
            n.coreYd = s.coreYd;
            if (s.kind == SettlementKind::Capital)
            {
                // A capital's centre has no height (CityDef): the anchor nearest to it (inside
                // the city: Ironforge is under the mountain, its roads leave through the gate).
                CityState const* city = cities.FindState(s.cityZone);
                CityAnchor const* best = nullptr;
                float bestDist = 0.0f;
                if (city)
                    for (CityAnchor const& a : city->anchors)
                    {
                        float const d = Dist2d(a.x, a.y, s.x, s.y);
                        if (!best || d < bestDist)
                        {
                            best = &a;
                            bestDist = d;
                        }
                    }
                if (!best)
                    continue;
                n.x = best->x;
                n.y = best->y;
                n.z = best->z;
            }
            nodes.push_back(n);
            m_nodeSize[s.id] = SizeOf(s.kind);
        }
        m_roads.Begin(std::move(nodes), std::move(hints));
        m_prewarmed.clear();
    }

    float TravellerDirector::PerHour(RoadEdge const& edge) const
    {
        auto const a = m_nodeSize.find(edge.nodeA);
        auto const b = m_nodeSize.find(edge.nodeB);
        float const sa = a != m_nodeSize.end() ? a->second : 0.5f;
        float const sb = b != m_nodeSize.end() ? b->second : 0.5f;
        return m_perHour * sa * sb / std::max(0.5f, edge.length / 1000.0f);
    }

    bool TravellerDirector::Fills(Observer const& o) const
    {
        if (o.travelling || (o.mapId != 0 && o.mapId != 1))
            return false;
        return !IsCityZone(o.zone);
    }

    bool TravellerDirector::UsableBy(RoadEdge const& edge, Settlements const& settlements, uint32 team) const
    {
        Settlement const* a = settlements.Find(edge.nodeA);
        Settlement const* b = settlements.Find(edge.nodeB);
        return a && b && a->ServesTeam(team) && b->ServesTeam(team);
    }

    std::vector<RoadEdge const*> TravellerDirector::RoadsFor(Observer const& o, Settlements const& settlements) const
    {
        std::vector<RoadEdge const*> out;
        for (RoadEdge const* e : m_roads.Near(o.mapId, o.x, o.y, NEAR_ROAD_YD))
            if (UsableBy(*e, settlements, o.team))
                out.push_back(e);
        return out;
    }

    float TravellerDirector::RateFor(Observer const& o, Settlements const& settlements, float hourFactor) const
    {
        float perHour = 0.0f;
        for (RoadEdge const* e : RoadsFor(o, settlements))
            perHour += PerHour(*e);
        return perHour * hourFactor / 3600.0f;
    }

    void TravellerDirector::LevelsOf(PopulationMgr& mgr, RoadEdge const& edge, uint8& lo, uint8& hi) const
    {
        lo = hi = 0;
        TownDirector& towns = mgr.GetTownDirector();
        for (uint32 id : { edge.nodeA, edge.nodeB })
        {
            TownState const* t = towns.FindState(id);
            if (!t || !t->levelMin)
                continue;
            lo = lo ? std::min(lo, t->levelMin) : t->levelMin;
            hi = std::max(hi, t->levelMax);
        }
    }

    bool TravellerDirector::BuildRequest(PopulationMgr& mgr, Observer const& o, Observers const& observers,
                                         RoadEdge const& edge, bool placeAnywhere, SpawnRequest& out) const
    {
        Map* map = sMapMgr.FindMap(o.mapId, 0);
        if (!map || edge.points.size() < 2)
            return false;

        float dist = 0.0f;
        float const here = RoadNetwork::Project(edge, o.x, o.y, dist);
        float const visible = map->GetVisibilityDistance();
        float const minYd = visible + SPAWN_BEYOND_MIN_YD, maxYd = visible + SPAWN_BEYOND_MAX_YD;

        // Where on the road (arc lengths), in the order to try.
        std::vector<float> candidates;
        if (placeAnywhere)
        {
            for (uint32 i = 0; i < PLACE_ANYWHERE_TRIES; ++i)
            {
                float const s = here + frand(-NEAR_COUNT_YD * 0.8f, NEAR_COUNT_YD * 0.8f);
                if (s > 0.0f && s < edge.length)
                    candidates.push_back(s);
            }
        }
        else
        {
            int32 first = urand(0, 1) ? 1 : -1;
            if (o.IsMoving())
            {
                // The road's way at his place: is he walking towards B or A?
                RoadPoint const p0 = RoadNetwork::PointAt(edge, here - 5.0f);
                RoadPoint const p1 = RoadNetwork::PointAt(edge, here + 5.0f);
                int32 const ahead = (p1.x - p0.x) * o.vx + (p1.y - p0.y) * o.vy >= 0.0f ? 1 : -1;
                first = urand(0, 99) < AHEAD_PCT ? ahead : -ahead;
            }
            for (int32 side : { first, -first })
            {
                for (float step = SEARCH_FIRST_YD; step <= SEARCH_LAST_YD; step += SEARCH_STEP_YD)
                {
                    float const s = here + float(side) * step;
                    if (s <= 0.0f || s >= edge.length)
                        break;
                    RoadPoint const p = RoadNetwork::PointAt(edge, s);
                    float const d = Dist2d(p.x, p.y, o.x, o.y);
                    if (d >= minYd && d <= maxYd)
                        candidates.push_back(s);
                }
            }
        }

        for (float s : candidates)
        {
            RoadPoint const p = RoadNetwork::PointAt(edge, s);
            float const z = map->GetHeight(p.x, p.y, p.z + 5.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - p.z) > 8.0f)
                continue;
            if (IsCityZone(sTerrainMgr.GetZoneId(o.mapId, p.x, p.y, z)))
                continue;
            if (!placeAnywhere && observers.CanSeeSoon(o.mapId, p.x, p.y, z, FieldDirector::LOGIN_LEAD_SEC))
                continue;

            // Towards him and on to that end of the road (the town there).
            bool const towardsA = placeAnywhere ? urand(0, 1) == 0 : s > here;
            out = SpawnRequest();
            out.route.push_back({ p.x, p.y, z });
            if (towardsA)
            {
                for (size_t i = edge.points.size(); i-- > 0;)
                    if (edge.along[i] < s)
                        out.route.push_back({ edge.points[i].x, edge.points[i].y, edge.points[i].z });
            }
            else
            {
                for (size_t i = 0; i < edge.points.size(); ++i)
                    if (edge.along[i] > s)
                        out.route.push_back({ edge.points[i].x, edge.points[i].y, edge.points[i].z });
            }
            if (out.route.size() < 2)
                continue;

            out.kind = BotKind::Traveller;
            out.team = o.team;
            out.mapId = o.mapId;
            out.x = p.x;
            out.y = p.y;
            out.z = z;
            out.o = std::atan2(out.route[1].y - p.y, out.route[1].x - p.x);
            if (out.o < 0.0f)
                out.o += 2.0f * float(M_PI);
            out.roadEdge = edge.id;
            out.rides = urand(0, 99) < m_mountPct;
            LevelsOf(mgr, edge, out.minLevel, out.maxLevel);
            out.raceMask = mgr.GetSettlements().StartRacesAt(o.mapId, p.x, p.y);
            out.prewarm = o.arriving;
            // The town it walks to (its hidden spots, should someone watch it arrive).
            out.settlementId = towardsA ? edge.nodeA : edge.nodeB;
            return true;
        }
        return false;
    }

    void TravellerDirector::Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs)
    {
        // One road per update until the network is built (each loads its navmesh tiles).
        if (m_roads.IsBuilding())
            m_roads.Step();

        if (nowMs < m_nextTickMs)
            return;
        m_nextTickMs = nowMs + TICK_MS;

        if (!m_enabled)
        {
            mgr.DespawnTravellers();
            return;
        }

        mgr.RetireTravellers(observers);

        // Arrived (in the world, or gone again): the next arrival gets its own.
        for (auto it = m_prewarmed.begin(); it != m_prewarmed.end();)
        {
            bool arriving = false;
            for (Observer const& o : observers.All())
                arriving |= o.guid == *it && o.arriving;
            it = arriving ? std::next(it) : m_prewarmed.erase(it);
        }

        Settlements const& settlements = mgr.GetSettlements();
        float const hourFactor = mgr.GetCityDirector().HourFactor();
        for (Observer const& o : observers.All())
        {
            if (!Fills(o))
                continue;
            if (o.arriving && m_prewarmed.count(o.guid))
                continue;
            std::vector<RoadEdge const*> const roads = RoadsFor(o, settlements);
            if (roads.empty())
                continue;
            uint32 const nearNow = mgr.CountTravellers(o, NEAR_COUNT_YD);
            if (nearNow >= m_maxNear)
                continue;

            float perHour = 0.0f;
            for (RoadEdge const* e : roads)
                perHour += PerHour(*e);
            float const rate = perHour * hourFactor / 3600.0f;

            uint32 want = 0;
            if (o.arriving)
            {
                // Already on the road when he comes in.
                m_prewarmed.insert(o.guid);
                want = std::min(m_maxNear - nearNow, Poisson(rate * 2.0f * NEAR_COUNT_YD / PASS_SPEED));
            }
            else if (frand(0.0f, 1.0f) < 1.0f - std::exp(-rate * float(TICK_MS) / 1000.0f))
                want = 1;

            for (uint32 i = 0; i < want; ++i)
            {
                RoadEdge const* edge = PickByRate(roads, *this);
                SpawnRequest request;
                if (!edge || !BuildRequest(mgr, o, observers, *edge, o.arriving, request) || !mgr.QueueSpawn(request))
                    break;
            }
        }
    }
}
