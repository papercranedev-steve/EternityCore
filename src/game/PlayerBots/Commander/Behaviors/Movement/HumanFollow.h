/*
 * PartyBot Commander layer 3 (cerebellum): follow the leader like a person (user, 2026-11-15).
 *
 * Before: every bot reacted in the same 100 ms check of FollowMovementGenerator, kept a fixed
 * offset at the leader's facing (a leader turning in place swung all of them around him),
 * walked to the exact point and turned to exactly the leader's facing at the end.
 *
 * Each bot gets a personality, fixed per character (from its guid, so it stays the same across
 * logins and differs between bots):
 *   - a preferred offset DIST_MIN..DIST_MAX behind the leader, SIDE_MIN..SIDE_MAX to its own
 *     side (left or right);
 *   - a reaction delay before a standing bot starts after the leader;
 *   - a band: it starts only when the leader is START_SLACK farther than its distance and,
 *     once the leader stops, stops wherever it is within STOP_SLACK of it;
 *   - attentive (turns to the leader once it leaves ATTENTIVE_ARC) or lazy (only when the
 *     leader is behind, LAZY_ARC), after a turn delay.
 * The offset is measured from the leader's direction of travel (FollowStyle.h, generator side).
 * FollowSpot still decides whether the offset is safe: PartyBotAI asks it with Heading() and
 * starts from PreferredOffset(); the first move of each MoveFollow is never held back.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_HUMAN_FOLLOW_H
#define MANGOS_COMMANDER_HUMAN_FOLLOW_H

#include "Common.h"
#include "FollowStyle.h"
#include "Commander/Behaviors/Movement/FollowSpot.h"

class Player;
class Unit;

namespace Commander
{
namespace HumanFollow
{
    constexpr float DIST_MIN = 2.5f;
    constexpr float DIST_MAX = 5.5f;
    constexpr float SIDE_MIN = 0.2f;              // radians off straight behind
    constexpr float SIDE_MAX = 1.1f;
    constexpr uint32 REACTION_MIN_MS = 200;
    constexpr uint32 REACTION_MAX_MS = 650;
    constexpr float START_SLACK_MIN = 1.5f;
    constexpr float START_SLACK_MAX = 4.0f;
    constexpr float STOP_SLACK_MIN = 0.3f;
    constexpr float STOP_SLACK_MAX = 2.0f;
    constexpr float ATTENTIVE_SHARE = 0.6f;
    constexpr float ATTENTIVE_ARC = 1.2f;         // radians, total width
    constexpr float LAZY_ARC = 3.0f;
    constexpr uint32 TURN_DELAY_MIN_MS = 300;
    constexpr uint32 TURN_DELAY_MAX_MS = 1500;

    struct Personality
    {
        FollowSpot::Offset offset;
        uint32 reactionMs = 0;
        float startSlack = 0.0f;
        float stopSlack = 0.0f;
        float faceArc = 0.0f;
        uint32 turnDelayMs = 0;
    };

    Personality Of(Player const* bot);

    // The direction the follow offset is measured from: the heading of the bot's styled follow
    // of `leader`, else the leader's facing (a new follow starts from it).
    float Heading(Player const* bot, Unit const* leader);

    FollowStyle StyleOf(Player const* bot, float heading);
}
}

#endif
