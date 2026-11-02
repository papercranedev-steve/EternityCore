#include "Commander/Coordination/LosBreak.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/CreatureProfile.h"
#include "Creature.h"
#include "Map.h"
#include <cmath>

namespace Commander
{
    namespace LosBreak
    {
        bool FindPoint(Map* map, Search const& search, Position3& point)
        {
            if (search.casters.empty() || !search.mover)
                return false;

            static float const TURNS[] = { 0.0f, 1.0472f, -1.0472f, 2.0944f, -2.0944f, 3.1416f };   // 0, +-60, +-120, 180 deg
            float const radii[] = { RING_INNER, RING_OUTER };
            Position3 const& center = search.center;
            Creature const* first = search.casters.front();
            float const away = std::atan2(center.y - first->GetPositionY(), center.x - first->GetPositionX());
            float const unitHeight = 2.0f;      // WorldObject::IsWithinLOS default target height

            uint32 checks = 0;
            // True = usable point. Stops counting at MAX_CHECKS.
            auto tryPoint = [&](Position3 const& p)
            {
                // Spell line of sight is checked from the caster's eyes to the target's position
                // + 1 yd (WorldObject::IsWithinLOSInMap, GetLosCheckPosition): same here.
                for (Creature const* c : search.casters)
                {
                    if (checks >= MAX_CHECKS)
                        return false;
                    ++checks;
                    if (c->IsWithinLOS(p.x, p.y, p.z, true, 1.0f))
                        return false;
                }
                if (checks >= MAX_CHECKS)
                    return false;
                ++checks;
                if (!search.mover->IsWithinLOS(p.x, p.y, p.z, true, unitHeight))
                    return false;
                for (Unit const* w : search.watchers)
                {
                    if (checks >= MAX_CHECKS)
                        return false;
                    ++checks;
                    if (!w->IsWithinLOS(p.x, p.y, p.z, true, unitHeight))
                        return false;
                }
                return true;
            };

            for (float const turn : TURNS)
            {
                for (float const radius : radii)
                {
                    if (checks >= MAX_CHECKS)
                        return false;
                    Position3 p;
                    p.x = center.x + std::cos(away + turn) * radius;
                    p.y = center.y + std::sin(away + turn) * radius;
                    // Ground near the center's height: no ledges, no floors above or below.
                    float const ground = map->GetHeight(p.x, p.y, center.z + 2.0f);
                    if (ground <= INVALID_HEIGHT || std::fabs(ground - center.z) > MAX_HEIGHT_DIFF)
                        continue;
                    p.z = ground;
                    if (search.rejects && search.rejects(p))
                        continue;
                    if (tryPoint(p))
                    {
                        point = p;
                        return true;
                    }
                }
            }
            return false;
        }

        bool IsRangedCaster(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile && profile->mainRangedSpellId;
        }
    }
}
