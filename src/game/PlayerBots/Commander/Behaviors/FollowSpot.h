/*
 * PartyBot Commander: where a bot follows the leader without pulling another pack.
 *
 * Before: PartyBotAI followed at a random 3-6 yd and a random angle around the leader's facing
 * (PB_MIN/MAX_FOLLOW_DIST/ANGLE) and never looked at idle creatures. In the Stockade the tank
 * followed 4 yd to the right front of the leader, saw into the right cell through its door and
 * pulled the prisoner there (17.6 yd, aggro range 20) while the leader opened the left cell
 * (stockade-20261101-174235).
 *
 * An offset is safe when, for every idle creature's aggro range + FOLLOW_MARGIN (SafeRoute /
 * AggroRange.h, line of sight included), none covers:
 *  - the follow point (leader position, offset at leader facing + angle, as
 *    FollowMovementGenerator computes it, on the ground there) and SPOT_SPREAD around it (line
 *    of sight changes within a step at a cell door; the margin only widens the distance);
 *  - while the leader moves, the follow points every LEAD_STEP_SEC along its facing up to
 *    `lookaheadSec` (one bot update plus the generator's FOLLOW_EXTRAPOLATION_SEC: the bot
 *    runs to where the leader will be);
 *  - the walk from the bot to the current follow point, every PATH_SAMPLE_STEP: the straight
 *    line up to STRAIGHT_WALK_DIST, else the navmesh path (a bot catching up from far behind
 *    runs through the middle of the corridor, not along the line to the point).
 * Choose() keeps the current offset while it is safe; else the first safe candidate behind the
 * leader (CANDIDATE_STEPS from straight behind x CANDIDATE_DISTS: the leader just walked there,
 * ahead nobody has looked yet); none safe or MAX_PATH_CHECKS used: FALLBACK_DIST straight
 * behind the leader.
 *
 * Cost: one grid scan around the leader, then per tested point a distance check per idle
 * creature and a line of sight check only for creatures in range, and at most MAX_PATH_CHECKS
 * pathfinding calls. PartyBotAI calls it when it starts following and then each bot update
 * while the leader moved FOLLOW_RECHECK_DIST or turned FOLLOW_RECHECK_TURN since the last check.
 * Candidates are only tried when the current offset is unsafe. Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_FOLLOW_SPOT_H
#define MANGOS_COMMANDER_FOLLOW_SPOT_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"

class Player;
class Unit;

namespace Commander
{
namespace FollowSpot
{
    // Covers the bot's bounding radius and the approximated follow point.
    float const FOLLOW_MARGIN = 3.0f;
    float const FOLLOW_RECHECK_DIST = 1.5f;
    float const FOLLOW_RECHECK_TURN = 0.35f;     // radians
    // FollowMovementGenerator::_setTargetLocation extrapolates the leader's movement 500 ms ahead.
    float const FOLLOW_EXTRAPOLATION_SEC = 0.5f;
    float const LEAD_STEP_SEC = 0.5f;
    // Ground lookup at a follow point: searched from this far above the leader (higher finds
    // low ceilings: the Stockade corridor ceiling is 6 yd up), used only within MAX_GROUND_STEP
    // of the leader's height (else the leader's height).
    float const GROUND_SEARCH_UP = 2.0f;
    float const MAX_GROUND_STEP = 4.0f;
    // Candidates: angle pi (behind the leader) + step, in this order, each at these distances.
    float const CANDIDATE_STEPS[] = { 0.0f, 0.785f, -0.785f, 1.571f, -1.571f };
    float const CANDIDATE_DISTS[] = { 3.0f, 4.5f, 6.0f };
    float const FALLBACK_DIST = 1.5f;
    // The stop point is checked with 4 more points this far around it.
    float const SPOT_SPREAD = 1.5f;
    // Walks up to this long are checked along the straight line, longer ones along the navmesh
    // path; at most MAX_PATH_CHECKS pathfinding calls per Choose().
    float const STRAIGHT_WALK_DIST = 8.0f;
    uint32 const MAX_PATH_CHECKS = 4;

    // MoveFollow parameters: distance from the leader and angle from its facing.
    struct Offset
    {
        float dist = 0.0f;
        float angle = 0.0f;
    };

    // Where FollowMovementGenerator sends `me` for this offset with the leader where it is now.
    Position3 PointOf(Player const* me, Unit const* leader, Offset const& o);

    // `current` when it is safe; else the safe candidate closest in angle; else behind the leader.
    Offset Choose(Player* me, Unit const* leader, Offset const& current, float lookaheadSec);

    // Smallest difference between two angles, 0..pi.
    float AngleGap(float a, float b);
}
}

#endif
