/*
 * PartyBot Commander layer 1: "would this idle creature attack `who` standing at a point".
 *
 * The one place for the proximity aggro rule (BasicAI::MoveInLineOfSight):
 *  - the creature is alive, not fighting, can start a fight (CanInitiateAttack), has no
 *    NO_AGGRO flag, is hostile to `who` and `who` is targetable by it;
 *  - the point is within GetAttackDistance(who) (detection_range +- level difference, 3D,
 *    no bounding radius) and, unless the creature flies, within CREATURE_Z_ATTACK_RANGE height;
 *  - the creature sees the point (IsWithinLOSInMap: from its eyes to 1 yd above the point).
 * Callers add a margin to the range for their own uncertainty (last step of a walk, follow
 * point approximation). Users: AssistPredictor (pull stand point), SafeRoute (flee and healer
 * spots and the walk there), FollowSpot (where a bot follows the leader).
 *
 * Reads live creatures: map thread of the creature.
 */

#ifndef MANGOS_COMMANDER_AGGRO_RANGE_H
#define MANGOS_COMMANDER_AGGRO_RANGE_H

#include "Commander/Perception/WorldModel.h"

class Creature;
class Unit;

namespace Commander
{
namespace AggroRange
{
    // Largest GetAttackDistance in practice: detection_range +- level difference capped at 25 levels.
    float const MAX_ATTACK_DISTANCE = 45.0f;
    // WorldObject::GetLosCheckPosition: a unit is seen at 1 yd above its feet.
    float const LOS_TARGET_HEIGHT = 1.0f;

    // Position-independent part: `c` is idle and would start a fight with `who` on sight.
    bool IsIdleAggressor(Creature const* c, Unit const* who);

    // GetAttackDistance(who) + margin; 0 when the creature has no aggro range against `who`.
    float Reach(Creature const* c, Unit const* who, float margin);

    // `at` is within `reach` of `c` (3D), within its aggro height and in its line of sight.
    // Distance first: the line of sight check only runs for points in range.
    bool Covers(Creature const* c, float reach, Position3 const& at);
}
}

#endif
