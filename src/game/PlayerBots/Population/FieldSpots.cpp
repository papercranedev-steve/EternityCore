#include "Population/FieldSpots.h"
#include "CreatureDefines.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include <algorithm>
#include <cmath>

namespace Population
{
    namespace
    {
        uint64 CellKey(int32 cx, int32 cy)
        {
            return (uint64(uint32(cx)) << 32) | uint32(cy);
        }

        int32 CellOf(float v)
        {
            return int32(std::floor(v / FieldSpots::CELL_YD));
        }

        bool IsHuntedCreature(CreatureInfo const* info)
        {
            if (!info || info->npc_flags || info->civilian || info->rank != CREATURE_ELITE_NORMAL)
                return false;
            if (info->flags_extra & CREATURE_FLAG_EXTRA_GUARD)
                return false;
            // Stable mounts (Theramore's horses), training dummies and town NPCs nobody levels on.
            if (info->static_flags1 & (CREATURE_STATIC_FLAG_MOUNTABLE | CREATURE_STATIC_FLAG_NO_XP | CREATURE_STATIC_FLAG_UNKILLABLE))
                return false;
            switch (info->type)
            {
                case CREATURE_TYPE_CRITTER:
                case CREATURE_TYPE_TOTEM:
                case CREATURE_TYPE_NOT_SPECIFIED:
                    return false;
            }
            return info->level_max > 0;
        }
    }

    FieldSpots::MapSpots const& FieldSpots::Of(uint32 mapId)
    {
        auto itr = m_maps.find(mapId);
        if (itr != m_maps.end())
            return itr->second;

        MapSpots& slot = m_maps[mapId];
        auto collect = [mapId, &slot](CreatureDataPair const& pair)
        {
            CreatureData const& d = pair.second;
            if (d.position.mapId != mapId || (d.spawn_flags & (SPAWN_FLAG_DISABLED | SPAWN_FLAG_DEAD)) || !d.creature_id[0])
                return false;
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(d.creature_id[0]);
            if (!IsHuntedCreature(info))
                return false;
            FieldSpot s;
            s.entry = info->entry;
            s.x = d.position.x;
            s.y = d.position.y;
            s.z = d.position.z;
            s.levelMin = uint8(std::min<uint32>(info->level_min, 255));
            s.levelMax = uint8(std::min<uint32>(info->level_max, 255));
            s.faction = info->faction;
            slot.cells[CellKey(CellOf(s.x), CellOf(s.y))].push_back(uint32(slot.spots.size()));
            slot.spots.push_back(s);
            return false;
        };
        sObjectMgr.DoCreatureData(collect);
        return slot;
    }

    std::vector<FieldSpot const*> FieldSpots::Around(uint32 mapId, float x, float y, float minYd, float maxYd)
    {
        std::vector<FieldSpot const*> out;
        MapSpots const& m = Of(mapId);
        int32 const reach = int32(std::ceil(maxYd / CELL_YD));
        int32 const cx = CellOf(x), cy = CellOf(y);
        float const minSq = minYd * minYd, maxSq = maxYd * maxYd;
        for (int32 dx = -reach; dx <= reach; ++dx)
        {
            for (int32 dy = -reach; dy <= reach; ++dy)
            {
                auto cell = m.cells.find(CellKey(cx + dx, cy + dy));
                if (cell == m.cells.end())
                    continue;
                for (uint32 index : cell->second)
                {
                    FieldSpot const& s = m.spots[index];
                    float const ddx = s.x - x, ddy = s.y - y;
                    float const distSq = ddx * ddx + ddy * ddy;
                    if (distSq >= minSq && distSq <= maxSq)
                        out.push_back(&s);
                }
            }
        }
        return out;
    }
}
