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
 * Tick: while walking the bot's place drifts a little; once the leader has stood a while the
 * bot looks around, takes a step or sits down (out of combat only).
 * FollowSpot still decides whether the offset is safe: TeamBotAI asks it with Heading() and
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

    // Drift (leader walking): every DRIFT_EVERY_MIN..MAX_MS a new offset DRIFT_DIST / DRIFT_ANGLE
    // around the preferred one (not a random walk: it stays near the bot's place, and a place
    // FollowSpot moved for safety goes back to it).
    constexpr uint32 DRIFT_EVERY_MIN_MS = 6000;
    constexpr uint32 DRIFT_EVERY_MAX_MS = 15000;
    constexpr float DRIFT_DIST = 0.8f;
    constexpr float DRIFT_ANGLE = 0.4f;
    // Idle (leader standing IDLE_AFTER_MIN..MAX_MS, per bot): every IDLE_EVERY_MIN..MAX_MS one
    // of: look around (LOOK_TURN either way), a step (a drift), sit down after SIT_AFTER_MS
    // (the generator stands the bot up when it starts after the leader).
    constexpr uint32 IDLE_AFTER_MIN_MS = 6000;
    constexpr uint32 IDLE_AFTER_MAX_MS = 15000;
    constexpr uint32 IDLE_EVERY_MIN_MS = 4000;
    constexpr uint32 IDLE_EVERY_MAX_MS = 12000;
    constexpr float LOOK_TURN = 1.3f;
    constexpr uint32 SIT_AFTER_MS = 25000;
    constexpr uint32 LOOK_CHANCE = 45;            // percent; then STEP_CHANCE, the rest sit
    constexpr uint32 STEP_CHANCE = 30;
    // The leader counts as standing while it stays within this of where it stopped.
    constexpr float STILL_YD = 0.5f;

    struct Personality
    {
        FollowSpot::Offset offset;
        uint32 reactionMs = 0;
        float startSlack = 0.0f;
        float stopSlack = 0.0f;
        float faceArc = 0.0f;
        uint32 turnDelayMs = 0;
        uint32 idleAfterMs = 0;
    };

    // Per bot (TeamBotAI member). Plain data.
    struct IdleState
    {
        uint32 nextActMs = 0;       // 0: not scheduled
        bool leaderStill = false;
        uint32 stillSinceMs = 0;
        float stillX = 0.0f;
        float stillY = 0.0f;
        bool sat = false;           // sat down by Tick (stood up again on combat)
    };

    Personality Of(Player const* bot);

    // The direction the follow offset is measured from: the heading of the bot's styled follow
    // of `leader`, else the leader's facing (a new follow starts from it).
    float Heading(Player const* bot, Unit const* leader);

    FollowStyle StyleOf(Player const* bot, float heading);

    // Every bot update with a leader; `following`: it follows `leader` with a style (after the
    // follow spot check). Looks around and sits by itself; true: follow at `next` instead (a
    // drift or a step; the caller checks it with FollowSpot). Not following or in combat:
    // nothing, and a bot it sat down stands up.
    bool Tick(Player* bot, Unit const* leader, IdleState& state, bool following, FollowSpot::Offset& next);
}
}

#endif
