#include "Population/CityPopulation.h"
#include "Population/Observers.h"
#include "Population/PopulationMgr.h"
#include "Population/PopulationIdentity.h"
#include "Population/ScenePoints.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "GameObjectDefines.h"
#include "GridMap.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "UnitDefines.h"
#include "Utilities/Random.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

namespace Population
{
    namespace
    {
        // 0:00 - 23:00, local time of the server machine.
        char const* const DEFAULT_HOUR_CURVE =
            "0.5 0.4 0.3 0.3 0.3 0.3 0.3 0.4 0.5 0.6 0.6 0.7 0.7 0.7 0.7 0.7 0.8 0.8 0.9 1.0 1.0 1.0 0.9 0.7";

        uint32 Clamp(int32 value, uint32 lo, uint32 hi)
        {
            if (value < int32(lo))
                return lo;
            if (uint32(value) > hi)
                return hi;
            return uint32(value);
        }

        bool ParseCurve(std::string text, float (&out)[24])
        {
            for (char& c : text)
                if (c == ',')
                    c = ' ';
            std::istringstream in(text);
            float values[24];
            uint32 n = 0;
            float v;
            while (in >> v)
            {
                if (n >= 24)
                    return false;
                values[n++] = std::max(0.0f, std::min(2.0f, v));
            }
            if (n != 24)
                return false;
            std::copy(values, values + 24, out);
            return true;
        }

        std::string JoinIds(std::vector<uint32> const& ids)
        {
            std::ostringstream out;
            for (size_t i = 0; i < ids.size(); ++i)
                out << (i ? "," : "") << ids[i];
            return out.str();
        }

        uint32 AnchorCapacity(std::string const& type)
        {
            if (type == "ah" || type == "bank")
                return 3;
            return 2;
        }

        // How many stand candidates an anchor type gets (auction house is the busiest).
        uint32 StandWeight(std::string const& type)
        {
            if (type == "ah")
                return 3;
            if (type == "bank")
                return 2;
            if (type == "mail" || type == "idle")
                return 1;
            return 0;   // showcase spots are for veterans only
        }

        template <class T>
        void Shuffle(std::vector<T>& v)
        {
            for (size_t i = v.size(); i > 1; --i)
                std::swap(v[i - 1], v[urand(0, uint32(i - 1))]);
        }

        float FacingTo(float x, float y, float tx, float ty)
        {
            float o = std::atan2(ty - y, tx - x);
            if (o < 0.0f)
                o += 2.0f * float(M_PI);
            return o;
        }
    }

    std::vector<CityDef> const& CityDirector::Cities()
    {
        static std::vector<CityDef> const cities = {
            { 1537, "ironforge",    "Ironforge",     0, -4928.0f,  -946.0f, 450.0f, ALLIANCE },
            { 1519, "stormwind",    "Stormwind",     0, -8829.0f,   625.0f, 550.0f, ALLIANCE },
            { 1637, "orgrimmar",    "Orgrimmar",     1,  1568.0f, -4405.0f, 550.0f, HORDE },
            { 1497, "undercity",    "Undercity",     0,  1586.0f,   239.0f, 350.0f, HORDE },
            { 1638, "thunderbluff", "Thunder Bluff", 1, -1278.0f,   123.0f, 450.0f, HORDE },
            { 1657, "darnassus",    "Darnassus",     1,  9947.0f,  2482.0f, 450.0f, ALLIANCE },
        };
        return cities;
    }

    CityDef const* CityDirector::FindCityByKey(std::string const& key)
    {
        for (CityDef const& def : Cities())
            if (key == def.key)
                return &def;
        return nullptr;
    }

    CityState const* CityDirector::FindState(uint32 zone) const
    {
        for (CityState const& s : m_states)
            if (s.def->zone == zone)
                return &s;
        return nullptr;
    }

    void CityDirector::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.City.Enable", true);
        m_maxBots = Clamp(sConfig.GetIntDefault("PopBot.City.MaxBots", 40), 0, 200);
        m_maxMoving = Clamp(sConfig.GetIntDefault("PopBot.City.MaxMoving", 12), 0, 100);
        m_showcase = Clamp(sConfig.GetIntDefault("PopBot.City.Showcase", 4), 0, 20);
        m_crafters = Clamp(sConfig.GetIntDefault("PopBot.City.Crafters", 6), 0, 50);
        m_emptyGraceMs = Clamp(sConfig.GetIntDefault("PopBot.City.EmptyGraceSec", 120), 0, 3600) * IN_MILLISECONDS;

        m_zones.clear();
        std::string zones = sConfig.GetStringDefault("PopBot.City.Zones", "1537 1519 1637");
        for (char& c : zones)
            if (c == ',')
                c = ' ';
        std::istringstream in(zones);
        uint32 zone;
        while (in >> zone)
            m_zones.push_back(zone);

        if (!ParseCurve(sConfig.GetStringDefault("PopBot.HourCurve", DEFAULT_HOUR_CURVE), m_hourCurve))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] PopBot.HourCurve needs 24 numbers, using the default curve.");
            ParseCurve(DEFAULT_HOUR_CURVE, m_hourCurve);
        }

        for (CityState& s : m_states)
            s.enabled = std::find(m_zones.begin(), m_zones.end(), s.def->zone) != m_zones.end();
    }

    void CityDirector::LoadAnchors(ScenePoints const& points)
    {
        std::vector<CityState> states;
        for (CityDef const& def : Cities())
        {
            CityState s;
            s.def = &def;
            s.enabled = std::find(m_zones.begin(), m_zones.end(), def.zone) != m_zones.end();
            // keep presence across reloads
            if (CityState const* old = FindState(def.zone))
            {
                s.playerPresent = old->playerPresent;
                s.lastPlayerSeenMs = old->lastPlayerSeenMs;
                s.everSeen = old->everSeen;
            }
            states.push_back(std::move(s));
        }

        auto cityAt = [&states](uint32 mapId, float x, float y) -> CityState*
        {
            for (CityState& s : states)
            {
                float const dx = x - s.def->centerX;
                float const dy = y - s.def->centerY;
                if (s.def->mapId == mapId && dx * dx + dy * dy <= s.def->radius * s.def->radius)
                    return &s;
            }
            return nullptr;
        };

        auto addAnchor = [](CityState& s, CityAnchor anchor) -> bool
        {
            // Same NPC spawned for several patches / duplicates: keep one.
            for (CityAnchor const& a : s.anchors)
                if (a.type == anchor.type && !a.exactSpot && std::fabs(a.x - anchor.x) < 1.0f && std::fabs(a.y - anchor.y) < 1.0f)
                    return false;
            s.anchors.push_back(std::move(anchor));
            return true;
        };

        uint32 const patch = sWorld.GetWowPatch();

        // Auctioneers and bankers (templates of the current patch are already in memory).
        std::vector<uint32> npcEntries;
        for (auto const& itr : sObjectMgr.GetCreatureInfoMap())
            if (itr.second && (itr.second->npc_flags & (UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_BANKER)))
                npcEntries.push_back(itr.first);

        uint32 dbCount = 0;
        if (!npcEntries.empty())
        {
            std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                "SELECT `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation` FROM `creature` "
                "WHERE `map` IN (0, 1) AND `patch_min` <= %u AND `patch_max` >= %u AND `id` IN (%s)",
                patch, patch, JoinIds(npcEntries).c_str());
            if (result)
            {
                do
                {
                    Field* f = result->Fetch();
                    CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(f[0].GetUInt32());
                    CityState* s = cityAt(f[1].GetUInt32(), f[2].GetFloat(), f[3].GetFloat());
                    if (!info || !s)
                        continue;
                    CityAnchor a;
                    a.type = (info->npc_flags & UNIT_NPC_FLAG_AUCTIONEER) ? "ah" : "bank";
                    a.label = info->name;
                    a.x = f[2].GetFloat();
                    a.y = f[3].GetFloat();
                    a.z = f[4].GetFloat();
                    a.o = f[5].GetFloat();
                    a.capacity = AnchorCapacity(a.type);
                    if (addAnchor(*s, std::move(a)))
                        ++dbCount;
                } while (result->NextRow());
            }
        }

        // Mailboxes.
        std::vector<uint32> mailEntries;
        for (auto const& itr : sObjectMgr.GetGameObjectInfoMap())
            if (itr.second && itr.second->type == GAMEOBJECT_TYPE_MAILBOX)
                mailEntries.push_back(itr.first);

        if (!mailEntries.empty())
        {
            std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                "SELECT `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation` FROM `gameobject` "
                "WHERE `map` IN (0, 1) AND `patch_min` <= %u AND `patch_max` >= %u AND `id` IN (%s)",
                patch, patch, JoinIds(mailEntries).c_str());
            if (result)
            {
                do
                {
                    Field* f = result->Fetch();
                    CityState* s = cityAt(f[1].GetUInt32(), f[2].GetFloat(), f[3].GetFloat());
                    if (!s)
                        continue;
                    CityAnchor a;
                    a.type = "mail";
                    a.label = "mailbox";
                    a.x = f[2].GetFloat();
                    a.y = f[3].GetFloat();
                    a.z = f[4].GetFloat();
                    a.o = f[5].GetFloat();
                    a.capacity = AnchorCapacity(a.type);
                    if (addAnchor(*s, std::move(a)))
                        ++dbCount;
                } while (result->NextRow());
            }
        }

        // Manual scene points (routes are read by movers from ScenePoints directly).
        uint32 manualCount = 0;
        for (ScenePoint const& p : points.All())
        {
            if (p.type == "route")
                continue;
            for (CityState& s : states)
            {
                if (p.city != s.def->key)
                    continue;
                CityAnchor a;
                a.type = p.type;
                a.label = "point #" + std::to_string(p.id);
                a.x = p.x;
                a.y = p.y;
                a.z = p.z;
                a.o = p.o;
                a.exactSpot = true;
                a.capacity = p.capacity;
                s.anchors.push_back(std::move(a));
                ++manualCount;
            }
        }

        m_states = std::move(states);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] City anchors: %u from the database, %u scene points.", dbCount, manualCount);
    }

    float CityDirector::HourFactor() const
    {
        time_t const now = time(nullptr);
        std::tm local;
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        return m_hourCurve[local.tm_hour % 24];
    }

    CityTargets CityDirector::TargetsNow() const
    {
        float const f = HourFactor();
        CityTargets t;
        t.total = uint32(std::lround(float(m_maxBots) * f));
        t.showcase = std::min(m_showcase, t.total);
        t.mover = std::min(uint32(std::lround(float(m_maxMoving) * f)), t.total - t.showcase);
        t.crafter = std::min(uint32(std::lround(float(m_crafters) * f)), t.total - t.showcase - t.mover);
        return t;
    }

    bool CityDirector::PickStandSpot(Map* map, CityAnchor const& anchor, float minDist, float maxDist, float spread, Waypoint& out)
    {
        if (anchor.exactSpot)
        {
            out.x = anchor.x;
            out.y = anchor.y;
            out.z = anchor.z;
            out.faceX = anchor.x + std::cos(anchor.o) * 5.0f;
            out.faceY = anchor.y + std::sin(anchor.o) * 5.0f;
            return true;
        }

        for (uint32 attempt = 0; attempt < 10; ++attempt)
        {
            float const dist = frand(minDist, maxDist);
            float const angle = anchor.o + frand(-spread, spread);
            float const x = anchor.x + std::cos(angle) * dist;
            float const y = anchor.y + std::sin(angle) * dist;
            float const z = map->GetHeight(x, y, anchor.z + 4.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - anchor.z) > 3.0f)
                continue;
            if (!map->isInLineOfSight(anchor.x, anchor.y, anchor.z + 2.0f, x, y, z + 2.0f))
                continue;
            out.x = x;
            out.y = y;
            out.z = z;
            out.faceX = anchor.x;
            out.faceY = anchor.y;
            return true;
        }
        return false;
    }

    bool CityDirector::PickHiddenSpot(uint32 zone, float x, float y, float z, Observers const& observers, Waypoint& out) const
    {
        float const MAX_WALK = 250.0f;

        CityState const* city = FindState(zone);
        if (!city)
            return false;
        Map* map = sMapMgr.FindMap(city->def->mapId, 0);
        if (!map)
            return false;

        bool found = false;
        float bestSq = MAX_WALK * MAX_WALK;
        auto consider = [&](float cx, float cy, float cz)
        {
            float const dx = cx - x, dy = cy - y;
            float const distSq = dx * dx + dy * dy;
            if (distSq >= bestSq || observers.CanSee(city->def->mapId, cx, cy, cz))
                return;
            bestSq = distSq;
            out = Waypoint();
            out.x = cx;
            out.y = cy;
            out.z = cz;
            out.faceX = x;
            out.faceY = y;
            found = true;
        };

        // Where people stand or walk anyway: reachable.
        for (CityAnchor const& a : city->anchors)
        {
            Waypoint w;
            if (PickStandSpot(map, a, 4.0f, 8.0f, float(M_PI), w))
                consider(w.x, w.y, w.z);
        }
        for (ScenePoint const& p : sPopulationMgr.GetScenePoints().All())
            if (p.city == city->def->key && p.mapId == city->def->mapId)
                consider(p.x, p.y, p.z);
        if (found)
            return true;

        // Open ground around the spot, on about the same level (no roofs or tunnels).
        for (float dist : { 35.0f, 60.0f, 90.0f, 130.0f })
        {
            for (uint32 i = 0; i < 12; ++i)
            {
                float const angle = float(i) * float(M_PI) / 6.0f + frand(-0.2f, 0.2f);
                float const cx = x + std::cos(angle) * dist;
                float const cy = y + std::sin(angle) * dist;
                float const cz = map->GetHeight(cx, cy, z + 5.0f);
                if (cz <= INVALID_HEIGHT || std::fabs(cz - z) > 6.0f)
                    continue;
                consider(cx, cy, cz);
            }
            if (found)
                return true;
        }
        return false;
    }

    bool CityDirector::BuildRequest(CityState const& city, CityRole role, CityCounts const& counts, SpawnRequest& out) const
    {
        Map* map = sMapMgr.FindMap(city.def->mapId, 0);
        if (!map)
            return false;

        out = SpawnRequest();
        out.kind = BotKind::City;
        out.cityZone = city.def->zone;
        out.role = role;
        out.team = city.def->team;
        out.mapId = city.def->mapId;
        out.tier = role == CityRole::Showcase ? GearTier::Showcase : GearTier::Normal;
        // 0: PopulationMgr::SpawnNow takes it from the roster (or a random city level without one).
        out.level = role == CityRole::Showcase ? 60 : 0;

        if (role == CityRole::Mover)
        {
            // A recorded route of this city half of the time, otherwise the stand spots.
            std::vector<ScenePoint> const& all = sPopulationMgr.GetScenePoints().All();
            std::vector<std::string> routeNames;
            for (ScenePoint const& p : all)
                if (p.type == "route" && p.city == city.def->key &&
                    std::find(routeNames.begin(), routeNames.end(), p.route) == routeNames.end())
                    routeNames.push_back(p.route);

            if (!routeNames.empty() && urand(0, 1))
            {
                std::string const& name = routeNames[urand(0, uint32(routeNames.size()) - 1)];
                for (ScenePoint const& p : all)    // already ordered by city, type, route, seq
                {
                    if (p.type != "route" || p.city != city.def->key || p.route != name)
                        continue;
                    Waypoint w;
                    w.x = p.x;
                    w.y = p.y;
                    w.z = p.z;
                    w.faceX = p.x + std::cos(p.o) * 5.0f;
                    w.faceY = p.y + std::sin(p.o) * 5.0f;
                    out.route.push_back(w);
                }
            }

            if (out.route.size() < 2)
            {
                out.route.clear();
                std::vector<CityAnchor const*> spots;
                for (CityAnchor const& a : city.anchors)
                    if (a.type == "ah" || a.type == "bank" || a.type == "mail" || a.type == "idle")
                        spots.push_back(&a);
                Shuffle(spots);
                for (CityAnchor const* a : spots)
                {
                    Waypoint w;
                    if (PickStandSpot(map, *a, 4.0f, 7.0f, a->type == "mail" ? float(M_PI) : 0.7f, w))
                        out.route.push_back(w);
                    if (out.route.size() >= 6)
                        break;
                }
            }

            if (out.route.size() < 2)
                return false;

            Waypoint const& start = out.route.front();
            out.x = start.x;
            out.y = start.y;
            out.z = start.z;
            out.hasFacePoint = true;
            out.faceX = start.faceX;
            out.faceY = start.faceY;
            out.o = FacingTo(start.x, start.y, start.faceX, start.faceY);
            return true;
        }

        if (role == CityRole::Crafter)
        {
            // Near the auction house / bank / mailbox, a little back from the stand spots,
            // facing the anchor. Does not take anchor capacity.
            std::vector<CityAnchor const*> spots;
            for (CityAnchor const& a : city.anchors)
                for (uint32 w = 0; w < StandWeight(a.type); ++w)
                    spots.push_back(&a);
            Shuffle(spots);
            for (CityAnchor const* a : spots)
            {
                Waypoint w;
                if (!PickStandSpot(map, *a, 6.0f, 11.0f, a->type == "mail" ? float(M_PI) : 1.1f, w))
                    continue;
                out.x = w.x;
                out.y = w.y;
                out.z = w.z;
                out.hasFacePoint = true;
                out.faceX = w.faceX;
                out.faceY = w.faceY;
                out.o = FacingTo(w.x, w.y, w.faceX, w.faceY);
                return true;
            }
            return false;
        }

        // Stand / showcase: an anchor with room left.
        std::vector<int32> candidates;
        for (size_t i = 0; i < city.anchors.size(); ++i)
        {
            CityAnchor const& a = city.anchors[i];
            if (i < counts.anchorUse.size() && counts.anchorUse[i] >= a.capacity)
                continue;
            uint32 weight = 0;
            if (role == CityRole::Showcase)
                weight = a.type == "showcase" ? 6 : (a.type == "ah" || a.type == "bank") ? 1 : 0;
            else
                weight = StandWeight(a.type);
            for (uint32 w = 0; w < weight; ++w)
                candidates.push_back(int32(i));
        }

        Shuffle(candidates);
        for (int32 index : candidates)
        {
            CityAnchor const& a = city.anchors[index];
            Waypoint w;
            bool const ok = role == CityRole::Showcase
                ? PickStandSpot(map, a, 8.0f, 12.0f, 1.0f, w)
                : PickStandSpot(map, a, 3.5f, 7.0f, a.type == "mail" ? float(M_PI) : 0.7f, w);
            if (!ok)
                continue;
            out.anchorIndex = index;
            out.x = w.x;
            out.y = w.y;
            out.z = w.z;
            out.hasFacePoint = true;
            out.faceX = w.faceX;
            out.faceY = w.faceY;
            out.o = FacingTo(w.x, w.y, w.faceX, w.faceY);
            return true;
        }
        return false;
    }

    void CityDirector::TickCity(PopulationMgr& mgr, CityState& city, uint32 nowMs)
    {
        if (city.playerPresent)
        {
            city.lastPlayerSeenMs = nowMs;
            city.everSeen = true;
        }

        CityCounts counts = mgr.CountCity(city.def->zone, city.anchors.size());

        if (!city.playerPresent)
        {
            if (counts.total && nowMs - city.lastPlayerSeenMs >= m_emptyGraceMs)
                mgr.DespawnCity(city.def->zone);
            return;
        }

        if (city.anchors.empty())
            return;

        CityTargets const targets = TargetsNow();

        // The hour changed and the city is too full: one leaves per tick.
        if (counts.total > targets.total + 2)
        {
            mgr.DespawnOneCity(city.def->zone);
            return;
        }

        // Someone is about to log in here: the whole crowd at once, before he can see it.
        // Otherwise the city fills a few at a time.
        uint32 const maxQueued = city.arriving ? targets.total : 3;
        for (uint32 queued = 0; queued < maxQueued && counts.total < targets.total; ++queued)
        {
            CityRole role = CityRole::Stand;
            if (counts.showcase < targets.showcase)
                role = CityRole::Showcase;
            else if (counts.mover < targets.mover)
                role = CityRole::Mover;
            else if (counts.crafter < targets.crafter)
                role = CityRole::Crafter;

            SpawnRequest request;
            if (!BuildRequest(city, role, counts, request))
            {
                // No free stand spot (or no route): try the other kind once.
                role = role == CityRole::Mover ? CityRole::Stand : CityRole::Mover;
                if (!BuildRequest(city, role, counts, request))
                    break;
            }
            request.prewarm = city.arriving;

            if (!mgr.QueueSpawn(request))
                break;

            ++counts.total;
            if (role == CityRole::Showcase)
                ++counts.showcase;
            else if (role == CityRole::Mover)
                ++counts.mover;
            else if (role == CityRole::Crafter)
                ++counts.crafter;
            else
                ++counts.stand;
            if (request.anchorIndex >= 0 && size_t(request.anchorIndex) < counts.anchorUse.size())
                ++counts.anchorUse[request.anchorIndex];
        }
    }

    void CityDirector::Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs)
    {
        if (nowMs < m_nextTickMs)
            return;
        m_nextTickMs = nowMs + 5 * IN_MILLISECONDS;

        if (!m_enabled)
        {
            for (CityState const& s : m_states)
                mgr.DespawnCity(s.def->zone);
            return;
        }

        for (CityState& s : m_states)
        {
            s.playerPresent = false;
            s.arriving = false;
        }

        for (Observer const& o : observers.All())
        {
            for (CityState& s : m_states)
            {
                if (s.def->zone != o.zone)
                    continue;
                s.playerPresent = true;
                s.arriving |= o.arriving;
            }
        }

        for (CityState& s : m_states)
        {
            if (s.enabled)
                TickCity(mgr, s, nowMs);
            else
                mgr.DespawnCity(s.def->zone);
        }
    }
}
