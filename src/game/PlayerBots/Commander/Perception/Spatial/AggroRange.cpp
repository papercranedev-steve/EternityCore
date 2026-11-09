#include "Commander/Perception/Spatial/AggroRange.h"
#include "Creature.h"
#include "Map.h"
#include <cmath>

namespace Commander
{
namespace AggroRange
{
    bool IsIdleAggressor(Creature const* c, Unit const* who)
    {
        if (!c->IsAlive() || c->IsInCombat() || !c->CanInitiateAttack())
            return false;
        if (c->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO))
            return false;
        return c->IsHostileTo(who) && who->IsTargetableBy(c);
    }

    float Reach(Creature const* c, Unit const* who, float margin)
    {
        float const attack = c->GetAttackDistance(who);
        return attack > 0.0f ? attack + margin : 0.0f;
    }

    bool Covers(Creature const* c, float reach, Position3 const& at)
    {
        if (reach <= 0.0f)
            return false;
        float const dx = c->GetPositionX() - at.x;
        float const dy = c->GetPositionY() - at.y;
        float const dz = c->GetPositionZ() - at.z;
        if (!c->CanFly() && std::fabs(dz) > CREATURE_Z_ATTACK_RANGE)
            return false;
        if (dx * dx + dy * dy + dz * dz > reach * reach)
            return false;
        // IsWithinLOSInMap looks at the target's GetLosCheckPosition: 1 yd above its feet.
        return c->IsWithinLOS(at.x, at.y, at.z, true, LOS_TARGET_HEIGHT);
    }

    Wander WanderOf(Creature const* c)
    {
        Wander w;
        w.home.x = c->GetPositionX();
        w.home.y = c->GetPositionY();
        w.home.z = c->GetPositionZ();
        if (c->GetDefaultMovementType() == RANDOM_MOTION_TYPE && c->GetWanderDistance() > 0.0f)
        {
            Position const& h = c->GetHomePosition();
            w.home.x = h.x;
            w.home.y = h.y;
            w.home.z = h.z;
            w.radius = c->GetWanderDistance();
        }
        return w;
    }

    bool CoversWandering(Creature const* c, float reach, Position3 const& at)
    {
        if (Covers(c, reach, at))
            return true;
        if (reach <= 0.0f)
            return false;
        Wander const w = WanderOf(c);
        if (w.radius <= 0.0f)
            return false;
        float const dx = w.home.x - at.x;
        float const dy = w.home.y - at.y;
        float const dz = w.home.z - at.z;
        if (!c->CanFly() && std::fabs(dz) > CREATURE_Z_ATTACK_RANGE)
            return false;
        float const r = reach + w.radius;
        if (dx * dx + dy * dy + dz * dz > r * r)
            return false;
        return c->GetMap()->isInLineOfSight(w.home.x, w.home.y, w.home.z + HOME_EYE_HEIGHT,
                                             at.x, at.y, at.z + LOS_TARGET_HEIGHT);
    }
}
}
