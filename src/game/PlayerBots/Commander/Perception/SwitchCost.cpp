#include "Commander/Perception/SwitchCost.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/CastValue.h"
#include <algorithm>

namespace Commander
{
namespace SwitchCost
{
    float Seconds(PartyMemberFacts const& member, EnemyFacts const& enemy, float reach)
    {
        if (member.victim == enemy.guid)
            return 0.0f;
        float sec = SWITCH_SEC;
        sec += member.castRemainingMs / 1000.0f;
        float const outOfReach = PackResolver::Distance(member.pos, enemy.pos) - reach;
        if (outOfReach > 0.0f)
            sec += outOfReach / CastValues::RUN_YARDS_PER_SEC;
        if (member.comboPoints && member.comboTarget != enemy.guid)
            sec += member.comboPoints * COMBO_POINT_SEC;
        return sec;
    }

    float Of(WorldModel const& world, PartyMemberFacts const& member, EnemyFacts const& enemy, float reach)
    {
        return Value::LostAction(world, member, Seconds(member, enemy, reach));
    }
}
}
