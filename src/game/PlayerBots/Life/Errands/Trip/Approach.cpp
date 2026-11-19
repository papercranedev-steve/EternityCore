#include "Life/Errands/Trip/Approach.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Timer.h"
#include <cmath>

namespace Life
{
namespace Trip
{
namespace Approach
{
    namespace
    {
        float const SHORT_YD = 2.5f;

        bool InReach(Player* me, WorldObject* target)
        {
            if (Creature* npc = target->ToCreature())
                return me->CanInteractWithNPC(npc, 0);
            return me->IsWithinDistInMap(target, INTERACTION_DISTANCE - 1.0f);
        }

        void Arrive(Player* me, ApproachState& state, uint32 now)
        {
            if (!me->IsStopped())
                me->StopMoving();
            state.arrivedMs = now;
        }

        void MoveTo(Player* me, Destination const& dest, ApproachState& state, float x, float y, float z, uint32 now)
        {
            if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) || now < state.nextMoveMs)
                return;
            state.nextMoveMs = now + REPATH_MS;
            uint32 const options = MOVE_PATHFINDING | (dest.run ? MOVE_RUN_MODE : MOVE_WALK_MODE);
            me->GetMotionMaster()->MovePoint(0, x, y, z, options);
        }
    }

    void Begin(ApproachState& state, uint32 now)
    {
        state.sinceMs = now;
        state.nextMoveMs = 0;
        state.arrivedMs = 0;
    }

    WorldObject* TargetOf(Player* me, ObjectGuid const& guid)
    {
        if (guid.IsGameObject())
            return me->GetMap()->GetGameObject(guid);
        Creature* npc = me->GetMap()->GetCreature(guid);
        return npc && npc->IsAlive() && !npc->IsInCombat() ? npc : nullptr;
    }

    ApproachResult Update(Player* me, Destination const& dest, ApproachState& state, uint32 now, uint32 giveUpMs)
    {
        if (state.arrivedMs)
            return ApproachResult::Arrived;

        bool const late = WorldTimer::getMSTimeDiff(state.sinceMs, now) > giveUpMs;
        if (!dest.target.IsEmpty())
        {
            WorldObject* target = TargetOf(me, dest.target);
            if (!target || late)
                return ApproachResult::Failed;
            if (InReach(me, target))
            {
                Arrive(me, state, now);
                me->SetFacingToObject(target);
                return ApproachResult::Arrived;
            }
            MoveTo(me, dest, state, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), now);
            return ApproachResult::Walking;
        }

        // (a path may end a little short of the point: standing that close counts)
        float const dist = me->GetDistance2d(dest.x, dest.y, SizeFactor::None);
        if (dist <= dest.reachYd || (dist <= dest.reachYd + SHORT_YD && !me->IsMoving()))
        {
            Arrive(me, state, now);
            if (dest.hasFace && (std::fabs(dest.faceX - me->GetPositionX()) > 0.1f || std::fabs(dest.faceY - me->GetPositionY()) > 0.1f))
                me->SetFacingTo(me->GetAngle(dest.faceX, dest.faceY));
            return ApproachResult::Arrived;
        }
        if (late)
            return ApproachResult::Failed;
        MoveTo(me, dest, state, dest.x, dest.y, dest.z, now);
        return ApproachResult::Walking;
    }
}
}
}
