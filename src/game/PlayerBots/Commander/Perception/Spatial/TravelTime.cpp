#include "Commander/Perception/Spatial/TravelTime.h"
#include "ObjectMgr.h"
#include "Map.h"
#include "SQLStorages.h"
#include <cmath>

namespace Commander
{
namespace TravelTime
{
    bool OutsidePoint(WorldLocation const& loc, WorldLocation& out)
    {
        MapEntry const* map = sMapStorage.LookupEntry<MapEntry>(loc.mapId);
        if (!map || !map->IsDungeon())
        {
            out = loc;
            return true;
        }
        if (AreaTriggerTeleport const* back = sObjectMgr.GetGoBackTrigger(loc.mapId))
        {
            out = back->destination;
            return true;
        }
        if (map->ghostEntranceMap >= 0)
        {
            out = WorldLocation(uint32(map->ghostEntranceMap), map->ghostEntranceX, map->ghostEntranceY, 0.0f);
            return true;
        }
        return false;
    }

    float Seconds(WorldLocation const& from, WorldLocation const& to, float speedYdPerSec)
    {
        if (speedYdPerSec <= 0.0f)
            return -1.0f;
        WorldLocation a, b;
        if (!OutsidePoint(from, a) || !OutsidePoint(to, b) || a.mapId != b.mapId)
            return -1.0f;
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy) * DETOUR_FACTOR / speedYdPerSec;
    }

    Journey JourneyOf(WorldLocation const& from, WorldLocation const& to, float runYdPerSec)
    {
        Journey j;
        WorldLocation a, b;
        if (runYdPerSec <= 0.0f || !OutsidePoint(from, a) || !OutsidePoint(to, b))
        {
            j.seconds = UNKNOWN_SEC;
            return j;
        }
        if (a.mapId != b.mapId)
        {
            // (a flight to the harbour and one from the other: the overheads, the flights' length
            // is not known)
            j.sails = true;
            j.flies = true;
            j.seconds = SAILING_SEC + 2.0f * FLIGHT_OVERHEAD_SEC;
            return j;
        }
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        float const yards = std::sqrt(dx * dx + dy * dy) * DETOUR_FACTOR;
        j.flies = yards > FLIGHT_FROM_YD;
        j.seconds = j.flies ? FLIGHT_OVERHEAD_SEC + yards / FLIGHT_SPEED : yards / runYdPerSec;
        return j;
    }
}
}
