#include "Commander/Perception/Spatial/LeapSpots.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Map.h"
#include "Player.h"
#include <cmath>
#include <memory>

namespace Commander
{
namespace LeapSpots
{
    std::vector<Spot> Find(Player* me, WorldModel const& world, float distance, Position3 const& awayFrom)
    {
        std::vector<Spot> out;
        if (distance <= 0.0f)
            return out;
        Map* map = me->GetMap();
        float const x = me->GetPositionX(), y = me->GetPositionY(), z = me->GetPositionZ();
        float const away = std::atan2(y - awayFrom.y, x - awayFrom.x);
        // One direction per cell on the circle of the jump.
        float const step = PositionField::CELL_YD / distance;
        int32 const steps = int32(std::ceil(float(M_PI) * 0.5f / step));
        PositionField const* field = world.field && !world.field->Empty() ? world.field.get() : nullptr;
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        float const fightSec = Value::FightRemainingSec(world);
        std::unique_ptr<SafeRoute::IdleCreatures> idle;     // only off the field

        for (int32 i = 0; i <= 2 * steps; ++i)
        {
            // 0, +1, -1, +2, -2 ... cells from straight away.
            int32 const k = (i + 1) / 2 * ((i & 1) ? 1 : -1);
            float o = away + float(k) * step;
            while (o < 0.0f)
                o += 2.0f * float(M_PI);
            while (o >= 2.0f * float(M_PI))
                o -= 2.0f * float(M_PI);
            Spot s;
            s.orientation = o;
            s.landing.x = x + std::cos(o) * distance;
            s.landing.y = y + std::sin(o) * distance;

            FieldCell const* cell = field ? field->At(s.landing) : nullptr;
            if (cell)
            {
                // The field's facts (standable and linked to the party, idle reach, patrol).
                if (!cell->standable || cell->idle ||
                    (cell->patrolSec >= 0.0f && cell->patrolSec < fightSec))
                    continue;
                s.landing.z = cell->z;
            }
            else
            {
                float const ground = map->GetHeight(s.landing.x, s.landing.y, z + MAX_STEP_Z);
                if (ground <= INVALID_HEIGHT)
                    continue;
                s.landing.z = ground;
                if (!idle)
                    idle.reset(new SafeRoute::IdleCreatures(me));
                if (idle->Near(s.landing) || PatrolZone::Comes(world, s.landing, fightSec))
                    continue;
            }
            if (std::fabs(s.landing.z - z) > MAX_STEP_Z || HazardZones::IsInside(zones, HAZARD_RANGED, s.landing))
                continue;
            // The jump is a straight line: nothing in between to stop it short.
            if (!map->isInLineOfSight(x, y, z + EYE_HEIGHT, s.landing.x, s.landing.y, s.landing.z + EYE_HEIGHT))
                continue;
            out.push_back(s);
        }
        return out;
    }
}
}
