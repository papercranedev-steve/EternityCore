#include "Commander/Behaviors/Movement/JumpCast.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace JumpCast
{
    namespace
    {
        void Face(Player* me, float o)
        {
            me->SetOrientation(o);
            me->m_movementInfo.ChangeOrientation(o);
            me->SendMovementPacket(MSG_MOVE_SET_FACING, false);
        }

        // The spell cast in the air has gone (an instant one goes at the next spell update).
        bool SpellGone(Player* me, SpellEntry const* spell)
        {
            Spell const* current = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            return !current || current->m_spellInfo != spell || current->getState() == SPELL_STATE_FINISHED;
        }

        void Land(PartyBotAI* bot, JumpCastState& state, bool resume)
        {
            Player* me = bot->me;
            state.active = false;
            me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FORWARD);
            me->m_movementInfo.fallTime = 0;
            me->m_movementInfo.jump = MovementInfo::JumpInfo();
            float const ground = me->GetMap()->GetHeight(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() + 3.0f);
            float z = me->GetPositionZ();
            if (ground > INVALID_HEIGHT)
                z = ground;
            me->SetPosition(me->GetPositionX(), me->GetPositionY(), z, state.moveFacing);
            me->SendMovementPacket(MSG_MOVE_FALL_LAND, false);
            if (resume && me->IsAlive() && !me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
                me->GetMotionMaster()->MovePoint(0, state.dest.x, state.dest.y, state.dest.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
    }

    bool Order(PartyBotAI* bot, JumpCastState& state, SpellEntry const* spell, float castFacing)
    {
        Player* me = bot->me;
        if (state.active || !spell || !me->IsAlive() || me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) ||
            me->IsNonMeleeSpellCasted(false, false, true) || me->GetTransport() || me->IsInWater() ||
            bot->IsBackpedaling() || me->movespline->Finalized() ||
            me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE ||
            me->m_movementInfo.HasMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR))
            return false;

        Movement::Vector3 const end = me->movespline->FinalDestination();
        state.dest.x = end.x;
        state.dest.y = end.y;
        state.dest.z = end.z;
        // Stop the spline where the bot is; from here it moves like a client.
        me->StopMoving(true);
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();

        state.active = true;
        state.start.x = me->GetPositionX();
        state.start.y = me->GetPositionY();
        state.start.z = me->GetPositionZ();
        state.moveFacing = me->GetAngle(state.dest.x, state.dest.y);
        state.castFacing = castFacing;
        state.speed = me->GetSpeed(MOVE_RUN);
        state.startMs = WorldTimer::getMSTime();
        state.spell = spell;
        state.cast = false;
        state.turnedBack = false;

        uint32 flags = me->GetUnitMovementFlags() & ~uint32(MOVEFLAG_MASK_MOVING | MOVEFLAG_WALK_MODE);
        me->SetUnitMovementFlags(flags | MOVEFLAG_FORWARD | MOVEFLAG_JUMPING);
        me->m_movementInfo.fallTime = 0;
        me->m_movementInfo.jump.zspeed = -JUMP_Z_SPEED;     // a client sends it negative (up)
        me->m_movementInfo.jump.cosAngle = std::cos(state.moveFacing);
        me->m_movementInfo.jump.sinAngle = std::sin(state.moveFacing);
        me->m_movementInfo.jump.xyspeed = state.speed;
        me->SetPosition(state.start.x, state.start.y, state.start.z, state.moveFacing);
        me->SendMovementPacket(MSG_MOVE_JUMP, false);
        return true;
    }

    void Update(PartyBotAI* bot, JumpCastState& state)
    {
        if (!state.active)
            return;
        Player* me = bot->me;
        if (!me->IsInWorld() || me->IsBeingTeleported() || !me->IsAlive())
        {
            state.active = false;
            return;
        }
        // Something else moves it, or it cannot move freely: down where it is.
        if (!me->movespline->Finalized() || me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE ||
            me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
        {
            Land(bot, state, false);
            return;
        }

        float const t = WorldTimer::getMSTimeDiff(state.startMs, WorldTimer::getMSTime()) / 1000.0f;
        float const x = state.start.x + std::cos(state.moveFacing) * state.speed * t;
        float const y = state.start.y + std::sin(state.moveFacing) * state.speed * t;
        float z = state.start.z + JUMP_Z_SPEED * t - 0.5f * GRAVITY * t * t;
        float const ground = me->GetMap()->GetHeight(x, y, std::max(z, state.start.z) + MAX_GROUND_STEP_YD);
        bool const down = t >= MAX_AIR_SEC ||
                          (t > JUMP_Z_SPEED / GRAVITY && ground > INVALID_HEIGHT && z <= ground);

        // The facing: to the cast, held until the spell has gone, then back.
        float o = state.moveFacing;
        if (t >= CAST_AT_SEC && !state.cast)
        {
            Face(me, state.castFacing);
            me->CastSpell(me, state.spell, false);
            state.cast = true;
        }
        if (state.cast && !state.turnedBack)
        {
            if (SpellGone(me, state.spell))
            {
                state.turnedBack = true;
                Face(me, state.moveFacing);
            }
            else
                o = state.castFacing;
        }

        me->m_movementInfo.fallTime = uint32(t * 1000.0f);
        me->SetPosition(x, y, down && ground > INVALID_HEIGHT ? ground : z, o);
        if (down)
            Land(bot, state, true);
    }
}
}
