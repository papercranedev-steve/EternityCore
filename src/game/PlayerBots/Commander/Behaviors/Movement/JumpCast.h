/*
 * PartyBot Commander layer 4 (hands): the jump turn ("跳吹", doc .kiro/docs/2026-11-12-mage-hold.md
 * 3.5). A running player jumps; in the air the way it moves is fixed and only its facing turns:
 * it turns round, casts an instant spell that goes the way it faces (Cone of Cold), turns back
 * and lands still running, at full speed the whole time.
 *
 * Order: the bot runs a point walk (MotionMaster MovePoint spline) and is free to move, not
 * casting, not swimming, not on a transport. The spline is stopped where the bot is (its final
 * point kept) and the bot moves like a client from then on, as Backpedal does:
 *   MSG_MOVE_JUMP with MOVEFLAG_FORWARD | MOVEFLAG_JUMPING and the jump info (z speed
 *   JUMP_Z_SPEED, sent negative as a client does; the walk's direction; run speed);
 *   every frame Player::SetPosition along that direction, height z0 + v t - g t^2 / 2;
 *   at CAST_AT_SEC: orientation to `castFacing`, MSG_MOVE_SET_FACING, the spell cast (an instant
 *   one goes off at the next spell update, so the facing is kept until the spell has gone);
 *   then the walk's direction again, MSG_MOVE_SET_FACING;
 *   on the ground (ground height reached after the top, or MAX_AIR_SEC): MSG_MOVE_FALL_LAND and
 *   the point walk to the kept final point again (MOVE_PATHFINDING | MOVE_RUN_MODE).
 * Ends early (lands where it is) when something else moves the bot or it cannot move freely.
 * False from Order: not running a point walk; the caller casts standing (DoCastFacing).
 *
 * Map thread of the bot (Update from PartyBotAI::UpdateAI every frame).
 */

#ifndef MANGOS_COMMANDER_JUMP_CAST_H
#define MANGOS_COMMANDER_JUMP_CAST_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"

class PartyBotAI;
class SpellEntry;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct JumpCastState
    {
        bool active = false;
        Position3 start;
        Position3 dest;                 // the point walk's final point (resumed on landing)
        float moveFacing = 0.0f;        // the way it moves (and faces again after the cast)
        float castFacing = 0.0f;
        float speed = 0.0f;
        uint32 startMs = 0;
        SpellEntry const* spell = nullptr;
        bool cast = false;              // the spell is out
        bool turnedBack = false;
    };

    namespace JumpCast
    {
        constexpr float JUMP_Z_SPEED = 7.95797334f;     // WorldSession::HandleMovementOpcodes
        constexpr float GRAVITY = 19.29110527f;
        constexpr float CAST_AT_SEC = 0.15f;
        constexpr float MAX_AIR_SEC = 1.2f;
        constexpr float MAX_GROUND_STEP_YD = 3.0f;

        // Header. False: not running a point walk (or not free to jump).
        bool Order(PartyBotAI* bot, JumpCastState& state, SpellEntry const* spell, float castFacing);

        // Every frame from PartyBotAI::UpdateAI.
        void Update(PartyBotAI* bot, JumpCastState& state);
    }
}

#endif
