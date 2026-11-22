#include "Population/LocalRaces.h"
#include "Population/PopulationTypes.h"
#include "GridMap.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <algorithm>

namespace Population
{
    namespace
    {
        constexpr uint32 Mask(uint8 race) { return 1u << (race - 1); }

        // The regions a race levels through first, and the capitals (AreaTable zone ids).
        // `level`: the zone's usual level, when the request has no level window.
        struct LocalZone
        {
            uint32 zone;
            uint32 races;
            uint8 level;
            bool capital;
        };

        LocalZone const ZONES[] = {
            // Alliance
            { 12,   Mask(RACE_HUMAN),                       8,  false },    // Elwynn Forest
            { 40,   Mask(RACE_HUMAN),                       15, false },    // Westfall
            { 44,   Mask(RACE_HUMAN),                       20, false },    // Redridge Mountains
            { 10,   Mask(RACE_HUMAN),                       25, false },    // Duskwood
            { 1,    Mask(RACE_DWARF) | Mask(RACE_GNOME),    8,  false },    // Dun Morogh
            { 38,   Mask(RACE_DWARF) | Mask(RACE_GNOME),    15, false },    // Loch Modan
            { 141,  Mask(RACE_NIGHTELF),                    8,  false },    // Teldrassil
            { 148,  Mask(RACE_NIGHTELF),                    16, false },    // Darkshore
            { 1519, Mask(RACE_HUMAN),                       0,  true },     // Stormwind City
            { 1537, Mask(RACE_DWARF) | Mask(RACE_GNOME),    0,  true },     // Ironforge
            { 1657, Mask(RACE_NIGHTELF),                    0,  true },     // Darnassus
            // Horde
            { 14,   Mask(RACE_ORC) | Mask(RACE_TROLL),      8,  false },    // Durotar
            { 215,  Mask(RACE_TAUREN),                      8,  false },    // Mulgore
            { 85,   Mask(RACE_UNDEAD),                      8,  false },    // Tirisfal Glades
            { 130,  Mask(RACE_UNDEAD),                      15, false },    // Silverpine Forest
            { 17,   Mask(RACE_ORC) | Mask(RACE_TROLL) | Mask(RACE_TAUREN), 17, false },    // The Barrens
            { 1637, Mask(RACE_ORC) | Mask(RACE_TROLL),      0,  true },     // Orgrimmar
            { 1638, Mask(RACE_TAUREN),                      0,  true },     // Thunder Bluff
            { 1497, Mask(RACE_UNDEAD),                      0,  true },     // Undercity
        };

        uint32 const CAPITAL_LOCAL_PCT = 70;

        // Locals by the place's levels: starting regions are home, the world mixes later.
        uint32 LocalPctForLevel(uint32 level)
        {
            if (level <= 13)
                return 95;
            if (level <= 20)
                return 85;
            if (level <= 30)
                return 60;
            return 0;
        }

        LocalZone const* FindZone(uint32 zone)
        {
            for (LocalZone const& z : ZONES)
                if (z.zone == zone)
                    return &z;
            return nullptr;
        }
    }

    uint32 LocalRaces::Roll(SpawnRequest const& r)
    {
        if (r.race || r.raceMask || r.kind == BotKind::Test || r.kind == BotKind::Herald)
            return 0;

        uint32 const zone = r.kind == BotKind::City && r.cityZone
            ? r.cityZone : sTerrainMgr.GetZoneId(r.mapId, r.x, r.y, r.z);
        LocalZone const* local = FindZone(zone);
        if (!local)
            return 0;

        uint32 const side = r.team == HORDE ? RACEMASK_HORDE : RACEMASK_ALLIANCE;
        uint32 const races = local->races & side;
        if (!races)
            return 0;   // the other side passing through: any of its races

        uint32 pct = CAPITAL_LOCAL_PCT;
        if (!local->capital)
        {
            uint32 const level = r.minLevel ? (uint32(r.minLevel) + std::max(r.minLevel, r.maxLevel)) / 2 : local->level;
            pct = LocalPctForLevel(level);
        }
        if (!pct)
            return 0;
        // The share is exact: newcomers are of the side's other races.
        return urand(0, 99) < pct ? races : side & ~races;
    }
}
