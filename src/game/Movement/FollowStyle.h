/*
 * Optional follow behaviour for FollowMovementGenerator (PartyBot, user 2026-11-15: following
 * looked robotic, every bot started in the same 100 ms, turned with the leader's facing and
 * stood on an exact point).
 *
 * Without a style the generator follows as before. With one:
 *  - heading frame: the offset angle is measured from the leader's direction of travel (the
 *    last direction it moved FollowStyle::HEADING_STEP yd in), not its facing. A leader that
 *    turns in place does not swing its followers around it;
 *  - a standing follower starts only when the leader is farther than offset + startSlack (a
 *    follower whose offset is in front of the leader: also when the leader moves and the
 *    follow point is startSlack away), and then after reactionMs (+-REACTION_JITTER);
 *  - once the leader stops, a follower within offset + stopSlack stops where it is;
 *  - faceArc > 0: no turn to the leader's facing at the end of a move; a standing follower
 *    turns to the leader after turnDelayMs when the leader is outside its faceArc.
 * The first move of a new generator (MoveFollow, Reset) is never held back: a caller that moves
 * the offset (FollowSpot: the old spot pulls a pack) gets the move at once.
 */

#ifndef MANGOS_FOLLOWSTYLE_H
#define MANGOS_FOLLOWSTYLE_H

#include "Common.h"

struct FollowStyle
{
    static constexpr float HEADING_STEP = 1.0f;
    static constexpr float REACTION_JITTER = 0.3f;

    float heading = 0.0f;           // initial direction of travel (radians)
    uint32 reactionMs = 0;
    float startSlack = 0.0f;
    float stopSlack = 0.5f;
    float faceArc = 0.0f;           // radians, total width; 0: turn to the leader's facing
    uint32 turnDelayMs = 0;
};

#endif
