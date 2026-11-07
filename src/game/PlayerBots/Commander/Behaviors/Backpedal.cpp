#include "Commander/Behaviors/Backpedal.h"
#include "Commander/Perception/SafeRoute.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "PathFinder.h"
#include "Player.h"
#include "Opcodes.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace Backpedal
{
    namespace
    {
        Position3 PositionOf(Player const* me)
        {
            Position3 p;
            p.x = me->GetPositionX();
            p.y = me->GetPositionY();
            p.z = me->GetPositionZ();
            return p;
        }

        float Distance2d(Position3 const& a, Position3 const& b)
        {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        }

        // Facing the walking direction a -> b (forward) or away from it (backwards).
        float WalkOrientation(Position3 const& a, Position3 const& b, bool forward)
        {
            float o = forward ? std::atan2(b.y - a.y, b.x - a.x) : std::atan2(a.y - b.y, a.x - b.x);
            if (o < 0.0f)
                o += 2.0f * float(M_PI);
            return o;
        }

        // A unit attacking `me` (its victim is `me`) is within `yards` (between bounding radii).
        bool AttackerWithin(Player const* me, float yards)
        {
            for (Unit const* attacker : me->GetAttackers())
                if (attacker->IsAlive() && attacker->IsInMap(me) && me->IsWithinDist(attacker, yards))
                    return true;
            return false;
        }

        bool BuildPath(Player* me, Position3 const& to, std::vector<Position3>& out)
        {
            out.clear();
            Position3 const here = PositionOf(me);
            PathInfo path(me);
            path.calculate(to.x, to.y, to.z);
            uint32 const type = path.getPathType();
            if (type & PATHFIND_NOPATH)
                return false;
            if (type & (PATHFIND_NOT_USING_PATH | PATHFIND_INCOMPLETE))
            {
                // No navmesh here, or the end is off the mesh: a straight walk when nothing is in
                // between (same fallback as SafeRoute::PathIsSafe).
                if (!me->GetMap()->isInLineOfSight(here.x, here.y, here.z + 2.0f, to.x, to.y, to.z + 2.0f))
                    return false;
                out.push_back(here);
                out.push_back(to);
                return true;
            }
            // A "normal" path can stop at the mesh edge, short of `to` (behind a wall): walking it
            // ends there and the next order finds no way on. Not reachable then.
            if (!SafeRoute::PathReaches(me->GetMap(), path, to))
                return false;
            out.push_back(here);
            for (auto const& p : path.getPath())
            {
                Position3 q;
                q.x = p.x;
                q.y = p.y;
                q.z = p.z;
                if (Distance2d(q, out.back()) >= 0.1f)
                    out.push_back(q);
            }
            // The last step off the mesh edge (PathReaches allows it), so the walk ends on `to`.
            if (Distance2d(to, out.back()) >= 0.1f)
                out.push_back(to);
            return out.size() >= 2;
        }

        bool Walking(Player const* me)
        {
            return me->m_movementInfo.HasMovementFlag(MOVEFLAG_BACKWARD | MOVEFLAG_FORWARD);
        }

        void SetWalking(Player* me, bool on, bool forward = false)
        {
            uint32 flags = me->GetUnitMovementFlags() & ~uint32(MOVEFLAG_MASK_MOVING);
            if (on)
                flags |= forward ? MOVEFLAG_FORWARD : MOVEFLAG_BACKWARD;
            me->SetUnitMovementFlags(flags);
        }

        // Stops walking (if walking) and tells the observers; `o` is the orientation to stand with.
        void StandStill(Player* me, float o)
        {
            bool const was = Walking(me);
            SetWalking(me, false);
            me->SetPosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), o);
            if (was)
                me->SendMovementPacket(MSG_MOVE_STOP, false);
            else
                me->SendMovementPacket(MSG_MOVE_SET_FACING, false);
        }

        // Something else drives the bot now: a MotionMaster generator, a spline, a player command.
        bool OtherMovement(Player* me)
        {
            if (!me->movespline->Finalized())
                return true;
            MovementGeneratorType const type = me->GetMotionMaster()->GetCurrentMovementGeneratorType();
            return type != IDLE_MOTION_TYPE;
        }
    }

    float WalkSec(Player const* me, float distance)
    {
        return distance / std::max(1.0f, me->GetSpeed(MOVE_RUN_BACK));
    }

    bool Order(PartyBotAI* bot, BackpedalState& state, Position3 const& to, ObjectGuid face, uint32 durationMs)
    {
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) || !me->IsAlive())
            return false;

        // Already there: nothing to walk (the caller's movement hold keeps the bot standing).
        if (Distance2d(PositionOf(me), to) <= ARRIVED_YD)
        {
            Stop(me, state);
            return true;
        }

        uint32 const now = WorldTimer::getMSTime();
        if (state.active && Distance2d(state.dest, to) <= SAME_DEST_YD && !OtherMovement(me))
        {
            state.orderMs = now;
            state.durationMs = durationMs;
            state.face = face;
            return true;
        }

        std::vector<Position3> path;
        if (!BuildPath(me, to, path))
        {
            if (state.active)
                Stop(me, state);
            return false;
        }

        // A spline or a generator would fight the position updates.
        if (!me->movespline->Finalized() || me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
        {
            me->StopMoving(true);
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
        }
        state.active = true;
        state.dest = to;
        state.face = face;
        state.path = std::move(path);
        state.next = 1;
        state.orderMs = now;
        state.durationMs = durationMs;
        state.lastBeatMs = 0;
        state.forward = false;
        return true;
    }

    void Stop(Player* me, BackpedalState& state)
    {
        if (!state.active)
            return;
        state.active = false;
        state.path.clear();
        if (!me->IsInWorld())
            return;
        float o = me->GetOrientation();
        if (!state.face.IsEmpty())
            if (Unit* face = me->GetMap()->GetUnit(state.face))
                if (face->IsAlive())
                    o = me->GetAngle(face);
        if (Walking(me) || o != me->GetOrientation())
            StandStill(me, o);
    }

    void Update(PartyBotAI* bot, BackpedalState& state, uint32 diff)
    {
        if (!state.active)
            return;
        Player* me = bot->me;
        if (!me->IsInWorld() || me->IsBeingTeleported() || !me->IsAlive())
        {
            state.active = false;
            state.path.clear();
            if (me->IsInWorld())
                SetWalking(me, false);
            return;
        }

        uint32 const now = WorldTimer::getMSTime();
        // Something else moves the bot: it owns the movement flags now.
        if (OtherMovement(me))
        {
            state.active = false;
            state.path.clear();
            return;
        }
        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) || WorldTimer::getMSTimeDiff(state.orderMs, now) >= state.durationMs ||
            state.next >= state.path.size())
        {
            Stop(me, state);
            return;
        }

        // A cast-time spell is being cast: moving would interrupt it. Stand until it is done.
        if (me->IsNonMeleeSpellCasted(false, false, true))
        {
            if (Walking(me))
                StandStill(me, me->GetOrientation());
            return;
        }

        // Forwards while nothing attacking it is close, backwards once something is (hysteresis).
        bool const forward = !AttackerWithin(me, state.forward ? TURN_AROUND_YD : RUN_FORWARD_YD);
        float const speed = me->GetSpeed(forward ? MOVE_RUN : MOVE_RUN_BACK);
        float left = std::max(1.0f, speed) * float(std::min(diff, MAX_STEP_MS)) / 1000.0f;
        Position3 at = PositionOf(me);
        size_t const startNext = state.next;
        while (left > 0.0f && state.next < state.path.size())
        {
            Position3 const& target = state.path[state.next];
            float const d = Distance2d(at, target);
            if (d <= left)
            {
                at = target;
                left -= d;
                ++state.next;
                continue;
            }
            float const f = left / d;
            at.x += (target.x - at.x) * f;
            at.y += (target.y - at.y) * f;
            at.z += (target.z - at.z) * f;
            left = 0.0f;
        }
        bool const corner = state.next != startNext;
        bool const arrived = state.next >= state.path.size() || Distance2d(at, state.dest) <= ARRIVED_YD;

        float const ground = me->GetMap()->GetHeight(at.x, at.y, at.z + 2.0f);
        if (ground > INVALID_HEIGHT && std::fabs(ground - at.z) <= MAX_GROUND_STEP_YD)
            at.z = ground;

        // Orientation: the segment walked now (turned around when walking backwards).
        float o = me->GetOrientation();
        if (state.next < state.path.size())
        {
            Position3 const& toward = state.path[state.next];
            if (Distance2d(at, toward) >= 0.1f)
                o = WalkOrientation(at, toward, forward);
        }

        // Starting, or turning around: MSG_MOVE_START_FORWARD / _BACKWARD from where it stands,
        // already turned (as a client does).
        bool const starting = !Walking(me) || forward != state.forward;
        if (starting)
        {
            state.forward = forward;
            SetWalking(me, true, forward);
            me->SetPosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), o);
            me->SendMovementPacket(forward ? MSG_MOVE_START_FORWARD : MSG_MOVE_START_BACKWARD, false);
            state.lastBeatMs = now;
        }

        me->SetPosition(at.x, at.y, at.z, o);

        if (arrived)
        {
            Stop(me, state);
            return;
        }
        if (!starting && (corner || WorldTimer::getMSTimeDiff(state.lastBeatMs, now) >= HEARTBEAT_MS))
        {
            me->SendHeartBeat(false);
            state.lastBeatMs = now;
        }
    }
}
}
