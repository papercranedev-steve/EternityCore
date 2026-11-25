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
 * spots and the walk there), FollowSpot (where a bot follows the leader), SeenOnRise (a body
 * or a feigning hunter getting up).
 *
 * Reads live creatures: map thread of the creature.
 */

#ifndef MANGOS_COMMANDER_AGGRO_RANGE_H
#define MANGOS_COMMANDER_AGGRO_RANGE_H

#include "Commander/Perception/WorldModel.h"
#include <list>

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

    // A creature that stands where it is (no default movement, not moving, idle motion now):
    // its aggro range is exact, the callers' margin is cut to STILL_MARGIN (2026-11-15, user:
    // stockade-20261115-012103, a Defias Prisoner standing in the corridor 21.1 yd from the
    // door's corner, aggro range about 20; the 1.5 yd hard line closed the door to the whole
    // party). STILL_MARGIN covers a walk's step off its path. Wanderers keep the margin and
    // their circle (CoversWandering); patrols are PatrolZone's.
    float const STILL_MARGIN = 0.5f;
    bool StandsStill(Creature const* c);

    // GetAttackDistance(who) + margin (STILL_MARGIN at most for a creature that stands still);
    // 0 when the creature has no aggro range against `who`.
    float Reach(Creature const* c, Unit const* who, float margin);

    // `at` is within `reach` of `c` (3D), within its aggro height and in its line of sight.
    // Distance first: the line of sight check only runs for points in range.
    bool Covers(Creature const* c, float reach, Position3 const& at);

    // Where a random wanderer (RANDOM_MOTION_TYPE with a wander distance) walks around:
    // `radius` 0 = it does not wander (home = its position).
    struct Wander
    {
        Position3 home;
        float radius = 0.0f;
    };
    Wander WanderOf(Creature const* c);

    // Covers, or for a random wanderer: `at` within reach + wander radius of its home, within
    // its aggro height from there, and in sight from its home (HOME_EYE_HEIGHT above it).
    // A spot a bot stands on for seconds: the wanderer walks to the edge of its circle
    // (strat-20261107-152527: the mage stood 20.3 yd from a Ravaged Cadaver's home, aggro range
    // 17, wander 5; it stepped closer after 14 s and brought its pack of seven).
    float const HOME_EYE_HEIGHT = 2.0f;
    bool CoversWandering(Creature const* c, float reach, Position3 const& at);

    // A bot that is not up yet: a body before its own resurrection (Soulstone, Reincarnation,
    // the corpse reclaim: CorpseRun) or a hunter in Feign Death (TeamBotAI::ShouldEndFeignDeath).
    // Would an idle creature start a fight once it stands at `at`? The creatures in `nearby`
    // (CoversWandering; IsIdleAggressor without the targetable test: a body or a feigning
    // hunter is not targetable now) and the patrols of `world` (nullable) whose predicted track
    // (PatrolFacts::track) comes within reach of `at` in the next RISE_PATROL_AHEAD_SEC.
    // strat-20261126-000902: after the wipe the hunter stood up with a Rockwing Gargoyle
    // patrol about 15 yd away and pulled it onto the soulstoned healer.
    float const RISE_MARGIN = 5.0f;
    float const RISE_PATROL_AHEAD_SEC = 10.0f;
    // The scan radius for `nearby` around the bot.
    float const RISE_SCAN_YD = MAX_ATTACK_DISTANCE + RISE_MARGIN;
    bool SeenOnRise(Unit const* who, std::list<Unit*> const& nearby, WorldModel const* world, Position3 const& at);
    // Same, `nearby` scanned around `who` (RISE_SCAN_YD), `at` = where it lies.
    bool SeenOnRise(Unit const* who, WorldModel const* world);
}
}

#endif
