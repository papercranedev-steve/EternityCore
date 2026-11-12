#include "Commander/Perception/Spatial/LeapSpots.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Map.h"
#include "Player.h"
#include <cmath>

namespace Commander
{
namespace LeapSpots
{
    std::vector<Spot> Find(Player* me, WorldModel const& world, float distance, Position3 const& awayFrom)
    {
        std::vector<Spot> out;
        Map* map = me->GetMap();
        float const x = me->GetPositionX(), y = me->GetPositionY(), z = me->GetPositionZ();
        float const away = std::atan2(y - awayFrom.y, x - awayFrom.x);
        float const step = float(M_PI) / 6.0f;
        float const offsets[] = { 0.0f, step, -step, 2.0f * step, -2.0f * step, 3.0f * step, -3.0f * step };
        SafeRoute::IdleCreatures const idle(me);
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        float const fightSec = Value::FightRemainingSec(world);
        for (float offset : offsets)
        {
            float o = away + offset;
            while (o < 0.0f)
                o += 2.0f * float(M_PI);
            while (o >= 2.0f * float(M_PI))
                o -= 2.0f * float(M_PI);
            Spot s;
            s.orientation = o;
            s.landing.x = x + std::cos(o) * distance;
            s.landing.y = y + std::sin(o) * distance;
            float const ground = map->GetHeight(s.landing.x, s.landing.y, z + MAX_STEP_Z);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - z) > MAX_STEP_Z)
                continue;
            s.landing.z = ground;
            if (!map->isInLineOfSight(x, y, z + EYE_HEIGHT, s.landing.x, s.landing.y, s.landing.z + EYE_HEIGHT))
                continue;
            if (idle.Near(s.landing) || HazardZones::IsInside(zones, HAZARD_RANGED, s.landing) ||
                PatrolZone::Comes(world, s.landing, fightSec))
                continue;
            out.push_back(s);
        }
        return out;
    }
}
}
