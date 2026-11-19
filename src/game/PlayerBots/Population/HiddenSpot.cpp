#include "Population/HiddenSpot.h"
#include "Population/Observers.h"
#include "GridMap.h"
#include "Map.h"
#include "Utilities/Random.h"
#include <cmath>

namespace Population
{
    bool PickHiddenSpot(Map* map, std::vector<Waypoint> const& known, float x, float y, float z,
        Observers const& observers, Waypoint& out)
    {
        float const MAX_WALK = 250.0f;
        if (!map)
            return false;
        uint32 const mapId = map->GetId();

        bool found = false;
        float bestSq = MAX_WALK * MAX_WALK;
        auto consider = [&](float cx, float cy, float cz)
        {
            float const dx = cx - x, dy = cy - y;
            float const distSq = dx * dx + dy * dy;
            if (distSq >= bestSq || observers.CanSee(mapId, cx, cy, cz))
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

        for (Waypoint const& w : known)
            consider(w.x, w.y, w.z);
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
}
