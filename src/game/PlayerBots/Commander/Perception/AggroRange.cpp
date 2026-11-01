#include "Commander/Perception/AggroRange.h"
#include "Creature.h"
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
}
}
