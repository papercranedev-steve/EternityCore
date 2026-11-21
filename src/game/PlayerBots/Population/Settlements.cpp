#include "Population/Settlements.h"
#include "Population/CityPopulation.h"
#include "Population/ScenePoints.h"
#include "GridMap.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "UnitDefines.h"
#include <algorithm>
#include <cmath>

namespace Population
{
    namespace
    {
        // Sources this close to a settlement (or inside its core) belong to it.
        float const MERGE_YD = 200.0f;
        // How far from its start spot a start area reaches: the valley (Northshire, Coldridge,
        // Valley of Trials ...), short of the next town (Goldshire ~545 yd, Dolanaar ~475 yd).
        float const START_AREA_YD = 450.0f;
        // Player faction templates of each side (Human, Orc) to test NPC factions against.
        uint32 const ALLIANCE_PLAYER_FACTION = 1;
        uint32 const HORDE_PLAYER_FACTION = 2;

        struct KindShape
        {
            float coreYd, falloffYd, weight;
        };

        // Capitals take their city radius as core (Cities()).
        KindShape ShapeOf(SettlementKind kind)
        {
            switch (kind)
            {
                case SettlementKind::Capital:   return { 0.0f, 700.0f, 1.0f };
                case SettlementKind::StartArea: return { 120.0f, 400.0f, 1.0f };
                case SettlementKind::Town:      return { 80.0f, 450.0f, 0.7f };
                case SettlementKind::Camp:      return { 60.0f, 300.0f, 0.4f };
                case SettlementKind::Manual:    return { 80.0f, 450.0f, 0.7f };
            }
            return { 80.0f, 450.0f, 0.7f };
        }

        Settlement Make(SettlementKind kind, uint32 mapId, float x, float y, float z, std::string label)
        {
            KindShape const shape = ShapeOf(kind);
            Settlement s;
            s.kind = kind;
            s.mapId = mapId;
            s.x = x;
            s.y = y;
            s.z = z;
            s.coreYd = shape.coreYd;
            s.falloffYd = shape.falloffYd;
            s.weight = shape.weight;
            s.label = std::move(label);
            return s;
        }

        // Which sides may gather at an NPC of faction template `factionId`: those it is not
        // hostile to.
        void SidesOf(uint32 factionId, bool& alliance, bool& horde)
        {
            FactionTemplateEntry const* ft = sObjectMgr.GetFactionTemplateEntry(factionId);
            FactionTemplateEntry const* ally = sObjectMgr.GetFactionTemplateEntry(ALLIANCE_PLAYER_FACTION);
            FactionTemplateEntry const* horde_ = sObjectMgr.GetFactionTemplateEntry(HORDE_PLAYER_FACTION);
            alliance = ft && ally && !ft->IsHostileTo(*ally);
            horde = ft && horde_ && !ft->IsHostileTo(*horde_);
        }

        float Dist2d(Settlement const& s, uint32 mapId, float x, float y)
        {
            if (s.mapId != mapId)
                return -1.0f;
            float const dx = s.x - x, dy = s.y - y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // Joins `source` to a settlement nearby (any stronger or equal kind comes first in
        // `all`, sources are added in kind order), else adds it.
        void Add(std::vector<Settlement>& all, Settlement source)
        {
            for (Settlement& s : all)
            {
                float const d = Dist2d(s, source.mapId, source.x, source.y);
                if (d >= 0.0f && d <= std::max(s.coreYd, MERGE_YD))
                {
                    ++s.sources;
                    s.raceMask |= source.raceMask;
                    return;
                }
            }
            all.push_back(std::move(source));
        }
    }

    char const* SettlementKindName(SettlementKind kind)
    {
        switch (kind)
        {
            case SettlementKind::Capital:   return "capital";
            case SettlementKind::StartArea: return "start";
            case SettlementKind::Town:      return "town";
            case SettlementKind::Camp:      return "camp";
            case SettlementKind::Manual:    return "manual";
        }
        return "?";
    }

    bool Settlement::ServesTeam(uint32 team) const
    {
        return team == ALLIANCE ? alliance : team == HORDE ? horde : false;
    }

    void Settlements::Load(ScenePoints const& points)
    {
        std::vector<Settlement> all;

        // Capitals.
        for (CityDef const& def : CityDirector::Cities())
        {
            Settlement s = Make(SettlementKind::Capital, def.mapId, def.centerX, def.centerY, 0.0f, def.title);
            s.coreYd = def.radius;
            s.alliance = def.team == ALLIANCE;
            s.horde = def.team == HORDE;
            s.cityZone = def.zone;
            all.push_back(std::move(s));
        }

        // Start areas: where each race begins (classes share the spot).
        for (uint32 race = 1; race < MAX_RACES; ++race)
        {
            for (uint32 classId = 1; classId < MAX_CLASSES; ++classId)
            {
                PlayerInfo const* info = sObjectMgr.GetPlayerInfo(race, classId);
                if (!info || (info->mapId != 0 && info->mapId != 1))
                    continue;
                Settlement s = Make(SettlementKind::StartArea, info->mapId, info->positionX, info->positionY, info->positionZ, "start area");
                uint32 const team = Player::TeamForRace(uint8(race));
                s.alliance = team == ALLIANCE;
                s.horde = team == HORDE;
                s.raceMask = 1u << (race - 1);
                Add(all, std::move(s));
                break;
            }
        }

        // Innkeepers (towns), then flight masters (camps) of the continents.
        struct NpcSource
        {
            SettlementKind kind;
            uint32 mapId;
            float x, y, z;
            uint32 faction;
            std::string name;
        };
        std::vector<NpcSource> npcs;
        auto collect = [&npcs](CreatureDataPair const& pair)
        {
            CreatureData const& d = pair.second;
            if ((d.position.mapId != 0 && d.position.mapId != 1) || (d.spawn_flags & SPAWN_FLAG_DISABLED) || !d.creature_id[0])
                return false;
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(d.creature_id[0]);
            if (!info)
                return false;
            SettlementKind kind;
            if (info->npc_flags & UNIT_NPC_FLAG_INNKEEPER)
                kind = SettlementKind::Town;
            else if (info->npc_flags & UNIT_NPC_FLAG_FLIGHTMASTER)
                kind = SettlementKind::Camp;
            else
                return false;
            npcs.push_back({ kind, d.position.mapId, d.position.x, d.position.y, d.position.z, info->faction, info->name });
            return false;
        };
        sObjectMgr.DoCreatureData(collect);

        for (SettlementKind kind : { SettlementKind::Town, SettlementKind::Camp })
        {
            for (NpcSource const& n : npcs)
            {
                if (n.kind != kind)
                    continue;
                Settlement s = Make(kind, n.mapId, n.x, n.y, n.z, n.name);
                SidesOf(n.faction, s.alliance, s.horde);
                if (!s.alliance && !s.horde)
                    continue;   // hostile to every player (event or quest NPC)
                Add(all, std::move(s));
            }
        }

        // Manual hubs: places the data misses; both sides gather there.
        for (ScenePoint const& p : points.All())
        {
            if (p.type != "hub" || (p.mapId != 0 && p.mapId != 1))
                continue;
            Settlement s = Make(SettlementKind::Manual, p.mapId, p.x, p.y, p.z, "point #" + std::to_string(p.id));
            s.alliance = true;
            s.horde = true;
            Add(all, std::move(s));
        }

        for (size_t i = 0; i < all.size(); ++i)
            all[i].id = uint32(i + 1);
        m_all = std::move(all);

        uint32 byKind[5] = {};
        for (Settlement const& s : m_all)
            ++byKind[uint8(s.kind)];
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Settlements: %u (capitals %u, start areas %u, towns %u, camps %u, manual %u).",
            uint32(m_all.size()), byKind[0], byKind[1], byKind[2], byKind[3], byKind[4]);
    }

    Settlement const* Settlements::Find(uint32 id) const
    {
        return id && id <= m_all.size() ? &m_all[id - 1] : nullptr;
    }

    std::vector<std::pair<float, Settlement const*>> Settlements::Near(uint32 mapId, float x, float y, float rangeYd) const
    {
        std::vector<std::pair<float, Settlement const*>> out;
        for (Settlement const& s : m_all)
        {
            float const d = Dist2d(s, mapId, x, y);
            if (d < 0.0f)
                continue;
            float const edge = std::max(0.0f, d - s.coreYd);
            if (edge <= rangeYd)
                out.emplace_back(edge, &s);
        }
        std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        return out;
    }

    float Settlements::Pull(uint32 mapId, float x, float y, uint32 team, float falloffScale) const
    {
        float best = 0.0f;
        for (Settlement const& s : m_all)
        {
            if (!s.ServesTeam(team))
                continue;
            float const d = Dist2d(s, mapId, x, y);
            if (d < 0.0f)
                continue;
            float const edge = std::max(0.0f, d - s.coreYd);
            float const falloff = std::max(1.0f, s.falloffYd * falloffScale);
            // e^-8: nothing left worth counting.
            if (edge > falloff * 8.0f)
                continue;
            best = std::max(best, s.weight * std::exp(-edge / falloff));
        }
        return best;
    }

    uint32 Settlements::StartRacesAt(uint32 mapId, float x, float y) const
    {
        uint32 races = 0;
        for (Settlement const& s : m_all)
        {
            float const d = Dist2d(s, mapId, x, y);
            if (d < 0.0f)
                continue;
            if (s.kind == SettlementKind::StartArea)
            {
                if (d <= START_AREA_YD)
                    races = s.raceMask;
            }
            else if (d <= s.coreYd)
                return 0;   // a town at the valley's mouth: everybody
        }
        return races;
    }

    std::string Settlements::AreaName(Settlement const& s)
    {
        AreaEntry const* area = AreaEntry::GetById(sTerrainMgr.GetAreaId(s.mapId, s.x, s.y, s.z + 2.0f));
        return area && area->Name ? area->Name : "?";
    }
}
