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
}
}
