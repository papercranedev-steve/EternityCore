/*
 * PartyBot Commander layer 1: where the tank can open a ranged pull from.
 * Design: .kiro/docs/2026-11-01-pull-planning.md (3.7)
 *
 * A shot spot is a point from which `shooter` can hit `target` with its ranged pull and that
 * pulls nothing by standing there or walking there (strat-20261102-235355: the straight-line
 * point 22 yd from the target had no line of sight, the tank stepped 4 yd at a time towards
 * the target and stopped 15 yd from a wandering Ravaged Cadaver, inside its aggro range; the
 * pack came before the shot and the shot failed too close).
 *
 * Find: candidates on rings around the target, at combat distances RING_OFFSETS inside the
 * range (never inside the spell's minimum range + MIN_RANGE_EXTRA), in TURNS directions
 * starting from the side the shooter is on; farthest ring and smallest turn first. A
 * candidate is usable when
 *  - its ground is within MAX_HEIGHT_DIFF of the target's height;
 *  - no idle creature that would attack the shooter (AggroRange::IsIdleAggressor, the target
 *    and its pack included) reaches it: distance to the creature < its aggro range +
 *    AGGRO_MARGIN, or for a random wanderer distance to its home < that + its wander
 *    distance. Line of sight is not asked: the tank stands there for seconds and the
 *    creatures move;
 *  - with Request::standSec > 0: no patrolling idle creature (waypoint movement, the target's
 *    side included unless ignored) walks within its aggro range + AGGRO_MARGIN of it during
 *    the next standSec seconds (PatrolTracker; strat-20261107-195427: the priest's opening
 *    control spot lay on Patchwork Horror's path, it walked into her while the tank waited);
 *  - it is not within AVOID_RADIUS of a point the caller rules out (spots that failed live);
 *  - the target sees it (from the target's eyes to UNIT_HEIGHT above the point);
 *  - the walk there is safe (SafeRoute::PathIsSafe: reachable, at most MAX_DETOUR times the
 *    straight line, no idle creature's aggro range (+ AggroRange margin, line of sight rule)
 *    on the way).
 * At most MAX_LOS_CHECKS line of sight checks and MAX_PATHS path searches per call.
 *
 * FindFace: a face pull spot (2026-11-07, user: "let the target see the tank, it comes by
 * itself; do not run up to it"). Same checks, on rings FACE_INSIDE inside the target's own
 * aggro range against the tank (never closer than its combat reach + FACE_MIN_GAP), the target
 * and Request::ignore (its group, which comes anyway) left out of the idle creature checks.
 *
 * Facts only, no decisions: PullPlanner picks the target with it, PullBehavior walks there.
 * Reads live creatures and pathfinds: map thread, or the world thread while maps are idle.
 */

#ifndef MANGOS_COMMANDER_SHOT_SPOT_H
#define MANGOS_COMMANDER_SHOT_SPOT_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;
class Map;
class Player;

namespace Commander
{
namespace ShotSpot
{
    // Combat distances (spell range minus these) of the candidate rings, farthest first.
    float const RING_OFFSETS[] = { 1.0f, 4.0f, 7.0f, 10.0f };
    // Directions relative to "target -> shooter", in radians: 0, +-30, +-60, +-90 degrees.
    float const TURNS[] = { 0.0f, 0.5236f, -0.5236f, 1.0472f, -1.0472f, 1.5708f, -1.5708f };
    float const MIN_RANGE_EXTRA = 2.0f;
    float const MAX_HEIGHT_DIFF = 5.0f;
    // Aggro range margin at the spot: the server checks on every movement update, the tank's
    // last step and its bounding radius (same value as AssistPredictor::AGGRO_RANGE_MARGIN).
    float const AGGRO_MARGIN = 2.0f;
    float const AVOID_RADIUS = 3.0f;
    float const UNIT_HEIGHT = 2.0f;
    float const MAX_DETOUR = 1.8f;
    uint32 const MAX_LOS_CHECKS = 16;
    uint32 const MAX_PATHS = 4;
    // Patrols looked for this much further out per second of Request::standSec (a running creature).
    float const PATROL_SCAN_YD_PER_SEC = 7.0f;
    float const PATROL_SCAN_MAX_YD = 120.0f;
    // Face pull rings: this far inside the target's aggro range (the server checks on movement
    // updates; the target may have stepped back a little), first ring first.
    float const FACE_INSIDE[] = { 3.0f, 6.0f };
    // Closer than the target's combat reach + this is a walk-in, not a face pull.
    float const FACE_MIN_GAP = 5.0f;

    struct Request
    {
        Creature const* target = nullptr;
        Player* shooter = nullptr;          // walks there from where it stands now
        float maxRange = 0.0f;              // combat distance the shot is used from (Find)
        float minRange = 0.0f;              // the spell's minimum range (0 = none)
        std::vector<Position3> avoid;       // ruled out by the caller (within AVOID_RADIUS)
        // Seconds the shooter stands at the spot before the fight: no patrol may come within
        // reach meanwhile. 0 = patrols are checked where they stand now only.
        float standSec = 0.0f;
        // Creatures left out of the idle creature checks (they come with the pull anyway).
        std::vector<ObjectGuid> ignore;
    };

    // Why no spot was found.
    enum class Failure : uint8
    {
        // In check order: the furthest check some candidate reached.
        None,
        NoGround,       // no ground near the target's height
        Aggro,          // every candidate is inside some creature's aggro range
        Patrol,         // the candidates left are on a patrol's way within Request::standSec
        NoSight,        // the target does not see the candidates left
        NoPath,         // no safe walk to the candidates left
        Budget,         // check budget used up
    };

    struct Spot
    {
        Position3 at;
        float range = 0.0f;         // planned combat distance to the target
        // Smallest (distance - reach) over the idle creatures; NO_CREATURE_CLEARANCE when none
        // would attack the shooter.
        float clearance = 0.0f;
    };
    float const NO_CREATURE_CLEARANCE = 1000.0f;

    bool Find(Map* map, Request const& request, Spot& out, Failure* why = nullptr);
    // Face pull spot (see above); Spot::range = the distance from the target's centre.
    bool FindFace(Map* map, Request const& request, Spot& out, Failure* why = nullptr);

    // Live check for a step the shooter is about to take: no idle creature near `target`
    // reaches `at` (same rule as a candidate's aggro check).
    bool IsClear(Creature const* target, Player* shooter, Position3 const& at);

    char const* FailureName(Failure failure);
}
}

#endif
