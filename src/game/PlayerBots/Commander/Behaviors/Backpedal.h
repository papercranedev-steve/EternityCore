/*
 * PartyBot Commander: walking backwards while fighting (the tank brings a pack back, or moves
 * the fight off a patrol's path, without turning its back on the enemies).
 *
 * A MoveSpline (MotionMaster::MovePoint) turns the unit along the path: the server sees the
 * tank's back to the enemies (no parry or block from behind, no melee swings outside the
 * 120 degree front arc, Dazed from behind) and the client shows it running away. Instead the
 * bot moves like a player holding the back key: MOVEFLAG_BACKWARD, run-back speed, and the
 * same packets a client sends (MSG_MOVE_START_BACKWARD, MSG_MOVE_HEARTBEAT every
 * HEARTBEAT_MS and at every path corner, MSG_MOVE_STOP), with Player::SetPosition every frame
 * (grid relocation, interrupts) as WorldSession::HandleMoverRelocation does for a client.
 * The orientation is the walking direction turned around: the client moves other players
 * from their flags and orientation between packets, so this is what keeps its picture on the
 * path. The chasing enemies come along the same path, i.e. in front of the bot.
 *
 * The order comes from a module (BotActionType::Backpedal, executed by the arbiter) and lasts
 * `durationMs`; the module proposes it again every tick while it still wants it (same point:
 * the path is kept). The original rotation runs in the same tick (instant abilities while
 * walking; a cast-time spell pauses the walk until it is cast, Player::SetPosition would
 * interrupt it).
 *
 * Facing only matters while something can hit the bot. While no unit attacking it (its
 * attacker set: the pack chasing after a shot, a caster) is within RUN_FORWARD_YD, it runs
 * forwards along the same path (MOVEFLAG_FORWARD, run speed, MSG_MOVE_START_FORWARD): after a
 * ranged pull the pack was 25-30 yd away and the tank walked back at 4.5 yd/s, so it was
 * caught halfway and slowed by Hamstring (strat-20261103-004601). It turns around once one is
 * within TURN_AROUND_YD (hysteresis between the two).
 *
 * Ends at the point, when the order runs out, when the bot cannot move freely (root, stun,
 * fear, confuse, taxi), when something else moves it (a MotionMaster generator or spline:
 * player commands, another module's MoveTo) or on Stop (PartyBotAI::ClearMovementHold).
 *
 * Map thread of the bot (Update from PartyBotAI::UpdateAI every frame, before the throttle).
 */

#ifndef MANGOS_COMMANDER_BACKPEDAL_H
#define MANGOS_COMMANDER_BACKPEDAL_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class PartyBotAI;
class Player;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct BackpedalState
    {
        bool active = false;
        Position3 dest;
        ObjectGuid face;                // turned to at the end (empty = keep the orientation)
        std::vector<Position3> path;    // from the start point (index 0) to dest
        size_t next = 1;                // path point walked towards
        uint32 orderMs = 0;             // last Order (start or renewal)
        uint32 durationMs = 0;          // the order runs out this long after orderMs
        uint32 lastBeatMs = 0;
        bool forward = false;           // running forwards now (nothing attacking it is close)
    };

    namespace Backpedal
    {
        constexpr uint32 HEARTBEAT_MS = 500;
        constexpr float ARRIVED_YD = 0.5f;
        // A renewed order to a point this close to the current one keeps the path.
        constexpr float SAME_DEST_YD = 2.0f;
        // Ground looked up under every step; a larger difference keeps the path's height.
        constexpr float MAX_GROUND_STEP_YD = 3.0f;
        // Frame time used at most for one step (a lag spike must not jump the bot).
        constexpr uint32 MAX_STEP_MS = 400;
        // Turns to walk backwards when a unit attacking the bot comes this close (yards between
        // bounding radii): a chaser at run speed is in melee about 2 s later. Runs forwards again
        // when none is within RUN_FORWARD_YD.
        constexpr float TURN_AROUND_YD = 8.0f;
        constexpr float RUN_FORWARD_YD = 12.0f;

        // Starts the walk, or renews the order (same point within SAME_DEST_YD: keeps the path).
        // False when the bot cannot walk there (rooted, no path).
        bool Order(PartyBotAI* bot, BackpedalState& state, Position3 const& to, ObjectGuid face, uint32 durationMs);

        // Every frame from PartyBotAI::UpdateAI.
        void Update(PartyBotAI* bot, BackpedalState& state, uint32 diff);

        // Ends the walk now (MSG_MOVE_STOP when the bot was walking).
        void Stop(Player* me, BackpedalState& state);

        // Seconds to walk `distance` yards backwards (run-back speed of `me`).
        float WalkSec(Player const* me, float distance);
    }
}

#endif
