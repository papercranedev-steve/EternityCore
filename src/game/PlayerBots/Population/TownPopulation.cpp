#include "Population/TownPopulation.h"
#include "Population/HiddenSpot.h"
#include "Population/Observers.h"
#include "Population/PopulationMgr.h"
#include "Population/Settlements.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "GameObjectDefines.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "UnitDefines.h"
#include "Utilities/Random.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Population
{
    namespace
    {
        uint32 const TICK_MS = 5 * IN_MILLISECONDS;
        // Creatures this far beyond the core give the residents' levels.
        float const LEVEL_SCAN_YD = 400.0f;
        // Of the residents: sitting (chairs, fires), walking between the anchors.
        float const SITTER_SHARE = 0.4f;
        float const MOVER_SHARE = 0.15f;
        // Neutral towns: residents of the observer's side this often (percent).
        uint32 const OWN_SIDE_PCT = 75;

        // Residents at full hour factor and PopBot.Town.Scale 1 (user 2026-11-19: Goldshire
        // 10-15 in town, about 15-20 seen at its edge with the hunters around it).
        uint32 ResidentsOf(SettlementKind kind)
        {
            switch (kind)
            {
                case SettlementKind::Capital:   return 0;   // CityDirector
                case SettlementKind::StartArea: return 10;
                case SettlementKind::Town:      return 14;
                case SettlementKind::Camp:      return 4;
                case SettlementKind::Manual:    return 8;
            }
            return 0;
        }

        uint32 CapacityOf(std::string const& type)
        {
            if (type == "chair" || type == "trainer")
                return 1;
            if (type == "fire" || type == "inn")
                return 3;
            return 2;
        }

        // Stand candidates per anchor type (the smith is busy).
        uint32 StandWeight(std::string const& type)
        {
            if (type == "smith")
                return 3;
            if (type == "inn")
                return 2;
            if (type == "vendor" || type == "trainer" || type == "mail" || type == "flight")
                return 1;
            return 0;   // chairs and fires are for sitters
        }

        uint32 Clamp(int32 value, uint32 lo, uint32 hi)
        {
            if (value < int32(lo))
                return lo;
            if (uint32(value) > hi)
                return hi;
            return uint32(value);
        }

        std::string JoinIds(std::vector<uint32> const& ids)
        {
            std::ostringstream out;
            for (size_t i = 0; i < ids.size(); ++i)
                out << (i ? "," : "") << ids[i];
            return out.str();
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

        // A stand spot at the anchor: in front of an NPC, all around an object.
        bool StandSpotAt(Map* map, TownAnchor const& a, Waypoint& out)
        {
            return a.object
                ? CityDirector::PickStandSpot(map, a.spot, 1.5f, 4.0f, float(M_PI), out)
                : CityDirector::PickStandSpot(map, a.spot, 3.0f, 6.0f, 0.8f, out);
        }

        // The NPC anchor type of a creature template, nullptr: none.
        char const* NpcAnchorType(uint32 npcFlags)
        {
            if (npcFlags & UNIT_NPC_FLAG_INNKEEPER)
                return "inn";
            if (npcFlags & UNIT_NPC_FLAG_REPAIR)
                return "smith";
            if (npcFlags & UNIT_NPC_FLAG_FLIGHTMASTER)
                return "flight";
            if (npcFlags & UNIT_NPC_FLAG_TRAINER)
                return "trainer";
            if (npcFlags & UNIT_NPC_FLAG_VENDOR)
                return "vendor";
            return nullptr;
        }

        // The object anchor type of a gameobject template, nullptr: none.
        char const* ObjectAnchorType(GameObjectInfo const* info)
        {
            switch (info->type)
            {
                case GAMEOBJECT_TYPE_CHAIR:
                    return "chair";
                case GAMEOBJECT_TYPE_MAILBOX:
                    return "mail";
                case GAMEOBJECT_TYPE_SPELL_FOCUS:
                    // SpellFocusObject.dbc: 1 anvil, 3 forge, 4 cooking fire.
                    if (info->spellFocus.focusId == 1 || info->spellFocus.focusId == 3)
                        return "smith";
                    if (info->spellFocus.focusId == 4)
                        return "fire";
                    return nullptr;
            }
            return nullptr;
        }
    }

    void TownDirector::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Town.Enable", true);
        m_scale = float(Clamp(sConfig.GetIntDefault("PopBot.Town.ScalePct", 100), 0, 300)) / 100.0f;
        m_emptyGraceMs = Clamp(sConfig.GetIntDefault("PopBot.City.EmptyGraceSec", 120), 0, 3600) * IN_MILLISECONDS;
    }

    TownState const* TownDirector::FindState(uint32 settlementId) const
    {
        for (TownState const& s : m_states)
            if (s.settlement->id == settlementId)
                return &s;
        return nullptr;
    }

    void TownDirector::LoadAnchors(Settlements const& settlements)
    {
        std::vector<TownState> states;
        for (Settlement const& s : settlements.All())
        {
            if (!ResidentsOf(s.kind))
                continue;
            TownState t;
            t.settlement = &s;
            // keep presence across reloads
            for (TownState const& old : m_states)
            {
                if (old.settlement->mapId == s.mapId && std::fabs(old.settlement->x - s.x) < 1.0f && std::fabs(old.settlement->y - s.y) < 1.0f)
                {
                    t.playerPresent = old.playerPresent;
                    t.lastPlayerSeenMs = old.lastPlayerSeenMs;
                }
            }
            states.push_back(std::move(t));
        }

        auto townAt = [&states](uint32 mapId, float x, float y) -> TownState*
        {
            TownState* best = nullptr;
            float bestDist = 0.0f;
            for (TownState& t : states)
            {
                Settlement const& s = *t.settlement;
                if (s.mapId != mapId)
                    continue;
                float const d = std::sqrt((x - s.x) * (x - s.x) + (y - s.y) * (y - s.y));
                if (d <= s.coreYd + ANCHOR_MARGIN_YD && (!best || d < bestDist))
                {
                    best = &t;
                    bestDist = d;
                }
            }
            return best;
        };

        auto addAnchor = [](TownState& t, TownAnchor anchor) -> bool
        {
            // Same spawn for several patches / duplicates: keep one.
            for (TownAnchor const& a : t.anchors)
                if (a.spot.type == anchor.spot.type && std::fabs(a.spot.x - anchor.spot.x) < 1.0f && std::fabs(a.spot.y - anchor.spot.y) < 1.0f)
                    return false;
            t.anchors.push_back(std::move(anchor));
            return true;
        };

        uint32 const patch = sWorld.GetWowPatch();
        uint32 npcCount = 0, objectCount = 0;

        std::vector<uint32> npcEntries;
        for (auto const& itr : sObjectMgr.GetCreatureInfoMap())
            if (itr.second && NpcAnchorType(itr.second->npc_flags))
                npcEntries.push_back(itr.first);
        if (!npcEntries.empty())
        {
            std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                "SELECT `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation` FROM `creature` "
                "WHERE `map` IN (0, 1) AND `patch_min` <= %u AND `patch_max` >= %u AND `id` IN (%s) "
                "AND `guid` NOT IN (SELECT `guid` FROM `game_event_creature`)",
                patch, patch, JoinIds(npcEntries).c_str());
            if (result)
            {
                do
                {
                    Field* f = result->Fetch();
                    CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(f[0].GetUInt32());
                    TownState* t = townAt(f[1].GetUInt32(), f[2].GetFloat(), f[3].GetFloat());
                    char const* type = info ? NpcAnchorType(info->npc_flags) : nullptr;
                    if (!t || !type)
                        continue;
                    TownAnchor a;
                    a.spot.type = type;
                    a.spot.label = info->name;
                    a.spot.x = f[2].GetFloat();
                    a.spot.y = f[3].GetFloat();
                    a.spot.z = f[4].GetFloat();
                    a.spot.o = f[5].GetFloat();
                    a.spot.capacity = CapacityOf(a.spot.type);
                    if (addAnchor(*t, std::move(a)))
                        ++npcCount;
                } while (result->NextRow());
            }
        }

        std::vector<uint32> objectEntries;
        for (auto const& itr : sObjectMgr.GetGameObjectInfoMap())
            if (itr.second && ObjectAnchorType(itr.second.get()))
                objectEntries.push_back(itr.first);
        if (!objectEntries.empty())
        {
            std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                "SELECT `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation` FROM `gameobject` "
                "WHERE `map` IN (0, 1) AND `patch_min` <= %u AND `patch_max` >= %u AND `id` IN (%s) "
                "AND `guid` NOT IN (SELECT `guid` FROM `game_event_gameobject`)",
                patch, patch, JoinIds(objectEntries).c_str());
            if (result)
            {
                do
                {
                    Field* f = result->Fetch();
                    GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(f[0].GetUInt32());
                    TownState* t = townAt(f[1].GetUInt32(), f[2].GetFloat(), f[3].GetFloat());
                    char const* type = info ? ObjectAnchorType(info) : nullptr;
                    if (!t || !type)
                        continue;
                    TownAnchor a;
                    a.spot.type = type;
                    a.spot.label = info->name;
                    a.spot.x = f[2].GetFloat();
                    a.spot.y = f[3].GetFloat();
                    a.spot.z = f[4].GetFloat();
                    a.spot.o = f[5].GetFloat();
                    a.spot.capacity = CapacityOf(a.spot.type);
                    a.object = true;
                    if (info->type == GAMEOBJECT_TYPE_CHAIR)
                        a.chairEntry = info->id;
                    if (addAnchor(*t, std::move(a)))
                        ++objectCount;
                } while (result->NextRow());
            }
        }

        // The levels the place is for: what lives around it (middle half of the creatures).
        for (TownState& t : states)
        {
            Settlement const& s = *t.settlement;
            std::vector<uint8> lows, highs;
            for (FieldSpot const* spot : m_spots.Around(s.mapId, s.x, s.y, 0.0f, s.coreYd + LEVEL_SCAN_YD))
            {
                lows.push_back(spot->levelMin);
                highs.push_back(spot->levelMax);
            }
            if (lows.empty())
                continue;
            std::sort(lows.begin(), lows.end());
            std::sort(highs.begin(), highs.end());
            t.levelMin = uint8(Clamp(lows[lows.size() / 4], 1, 60));
            t.levelMax = uint8(Clamp(highs[highs.size() * 3 / 4] + 2, t.levelMin, 60));
        }

        m_states = std::move(states);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Town anchors: %u NPCs, %u objects in %u places.",
            npcCount, objectCount, uint32(m_states.size()));
    }

    TownTargets TownDirector::TargetsFor(TownState const& town, float hourFactor) const
    {
        uint32 seats = 0, room = 0;
        for (TownAnchor const& a : town.anchors)
        {
            room += a.spot.capacity;
            if (a.spot.type == "chair" || a.spot.type == "fire")
                seats += a.spot.capacity;
        }

        TownTargets t;
        t.total = std::min(room, uint32(std::lround(float(ResidentsOf(town.settlement->kind)) * m_scale * hourFactor)));
        t.sitter = std::min(seats, uint32(std::lround(float(t.total) * SITTER_SHARE)));
        t.mover = std::min(t.total - t.sitter, uint32(std::lround(float(t.total) * MOVER_SHARE)));
        return t;
    }

    bool TownDirector::PickHiddenSpot(uint32 settlementId, float x, float y, float z, Observers const& observers, Waypoint& out) const
    {
        TownState const* town = FindState(settlementId);
        if (!town)
            return false;
        Map* map = sMapMgr.FindMap(town->settlement->mapId, 0);
        if (!map)
            return false;

        std::vector<Waypoint> known;
        for (TownAnchor const& a : town->anchors)
        {
            Waypoint w;
            if (a.spot.type != "chair" && StandSpotAt(map, a, w))
                known.push_back(w);
        }
        return Population::PickHiddenSpot(map, known, x, y, z, observers, out);
    }

    bool TownDirector::BuildRequest(TownState const& town, CityRole role, CityCounts const& counts, SpawnRequest& out) const
    {
        Settlement const& s = *town.settlement;
        Map* map = sMapMgr.FindMap(s.mapId, 0);
        if (!map)
            return false;

        out = SpawnRequest();
        out.kind = BotKind::Town;
        out.settlementId = s.id;
        out.role = role;
        out.mapId = s.mapId;
        out.minLevel = town.levelMin;
        out.maxLevel = town.levelMax;
        // The side that lives here; both: mostly the observer's.
        if (s.alliance && s.horde)
        {
            uint32 const own = town.observerTeam == HORDE ? HORDE : ALLIANCE;
            out.team = urand(0, 99) < OWN_SIDE_PCT ? own : (own == HORDE ? ALLIANCE : HORDE);
        }
        else
            out.team = s.horde ? HORDE : ALLIANCE;

        auto hasRoom = [&](size_t i)
        {
            return i >= counts.anchorUse.size() || counts.anchorUse[i] < town.anchors[i].spot.capacity;
        };

        if (role == CityRole::Mover)
        {
            std::vector<TownAnchor const*> spots;
            for (TownAnchor const& a : town.anchors)
                if (StandWeight(a.spot.type))
                    spots.push_back(&a);
            Shuffle(spots);
            for (TownAnchor const* a : spots)
            {
                Waypoint w;
                if (StandSpotAt(map, *a, w))
                    out.route.push_back(w);
                if (out.route.size() >= 6)
                    break;
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

        // Sitter: a free chair (most of the time) or a place by a fire. Stand: an anchor with room.
        std::vector<int32> candidates;
        for (size_t i = 0; i < town.anchors.size(); ++i)
        {
            if (!hasRoom(i))
                continue;
            std::string const& type = town.anchors[i].spot.type;
            uint32 const weight = role == CityRole::Sitter
                ? (type == "chair" ? 3 : type == "fire" ? 1 : 0)
                : StandWeight(type);
            for (uint32 w = 0; w < weight; ++w)
                candidates.push_back(int32(i));
        }
        Shuffle(candidates);

        for (int32 index : candidates)
        {
            TownAnchor const& a = town.anchors[index];
            if (a.chairEntry)
            {
                // On the chair itself (the AI sits down on it: GameObject::Use).
                out.anchorIndex = index;
                out.chairEntry = a.chairEntry;
                out.x = a.spot.x;
                out.y = a.spot.y;
                out.z = a.spot.z;
                out.o = a.spot.o;
                return true;
            }

            Waypoint w;
            bool const ok = role == CityRole::Sitter
                ? CityDirector::PickStandSpot(map, a.spot, 2.0f, 4.0f, float(M_PI), w)
                : StandSpotAt(map, a, w);
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

    void TownDirector::TickTown(PopulationMgr& mgr, TownState& town, float hourFactor, uint32 nowMs)
    {
        PlaceKey const place = PlaceKey::Town(town.settlement->id);
        if (town.playerPresent)
            town.lastPlayerSeenMs = nowMs;

        CityCounts counts = mgr.CountPlace(place, town.anchors.size());
        if (!town.playerPresent)
        {
            if (counts.total && nowMs - town.lastPlayerSeenMs >= m_emptyGraceMs)
                mgr.DespawnPlace(place);
            return;
        }
        if (town.anchors.empty())
            return;

        TownTargets const targets = TargetsFor(town, hourFactor);

        // The hour changed: one leaves per tick.
        if (counts.total > targets.total + 2)
        {
            mgr.DespawnOnePlace(place);
            return;
        }

        // Someone is about to log in here: everybody at once. Otherwise a few at a time.
        uint32 const maxQueued = town.arriving ? targets.total : 3;
        for (uint32 queued = 0; queued < maxQueued && counts.total < targets.total; ++queued)
        {
            CityRole role = CityRole::Stand;
            if (counts.sitter < targets.sitter)
                role = CityRole::Sitter;
            else if (counts.mover < targets.mover)
                role = CityRole::Mover;

            SpawnRequest request;
            if (!BuildRequest(town, role, counts, request))
            {
                // No free seat / stand spot: try the other kind once.
                role = role == CityRole::Stand ? CityRole::Sitter : CityRole::Stand;
                if (!BuildRequest(town, role, counts, request))
                    break;
            }
            request.prewarm = town.arriving;
            if (!mgr.QueueSpawn(request))
                break;

            ++counts.total;
            if (role == CityRole::Sitter)
                ++counts.sitter;
            else if (role == CityRole::Mover)
                ++counts.mover;
            else
                ++counts.stand;
            if (request.anchorIndex >= 0 && size_t(request.anchorIndex) < counts.anchorUse.size())
                ++counts.anchorUse[request.anchorIndex];
        }
    }

    void TownDirector::Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs)
    {
        if (nowMs < m_nextTickMs)
            return;
        m_nextTickMs = nowMs + TICK_MS;

        if (!m_enabled)
        {
            for (TownState const& t : m_states)
                mgr.DespawnPlace(PlaceKey::Town(t.settlement->id));
            return;
        }

        for (TownState& t : m_states)
        {
            t.playerPresent = false;
            t.arriving = false;
            float nearest = 0.0f;
            for (Observer const& o : observers.All())
            {
                Settlement const& s = *t.settlement;
                if (o.travelling || o.mapId != s.mapId)
                    continue;
                float const edge = std::sqrt((o.x - s.x) * (o.x - s.x) + (o.y - s.y) * (o.y - s.y)) - s.coreYd;
                if (edge > ACTIVE_YD)
                    continue;
                if (!t.playerPresent || edge < nearest)
                {
                    nearest = edge;
                    t.observerTeam = o.team;
                }
                t.playerPresent = true;
                t.arriving |= o.arriving;
            }
        }

        float const hourFactor = mgr.GetCityDirector().HourFactor();
        for (TownState& t : m_states)
            TickTown(mgr, t, hourFactor, nowMs);
    }
}
