#include "Commander/Behaviors/Movement/Arrival.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Timer.h"
#include <limits>

namespace Commander
{
namespace Arrival
{
    void Start(ArrivalState& state, uint32 nowMs)
    {
        state = ArrivalState();
        state.active = true;
        state.deadlineMs = nowMs + MAX_MS;
        state.bestDist = std::numeric_limits<float>::max();
        state.bestMs = nowMs;
    }

    void Stop(ArrivalState& state)
    {
        state.active = false;
    }

    bool Update(PartyBotAI* bot, ArrivalState& state, Player* leader)
    {
        if (!state.active)
            return false;

        Player* me = bot->me;
        uint32 const now = WorldTimer::getMSTime();
        if (now >= state.deadlineMs)
        {
            Stop(state);
            return false;
        }

        // The leader runs his own corpse (a ghost): wait here, as a player does at the entrance.
        // The walk's clocks start again when he is back.
        // Fix (user, 2026-11-15): only a ghost. A leader whose body lies unreleased waits for a
        // resurrection: the bots waited at the entrance forever, and this return came before
        // the resurrection; the walk goes to the body instead (the dead leader's position).
        if (!leader->IsAlive() && leader->GetDeathState() != CORPSE)
        {
            bot->EndRestWhenFull();
            if (!bot->IsResting() && bot->NeedsRest())
                bot->StartResting();
            state.deadlineMs = now + MAX_MS;
            state.bestMs = now;
            return true;
        }

        if (!me->IsInMap(leader))
        {
            Stop(state);
            return false;
        }

        float const dist = me->GetDistance(leader);
        if (dist <= DONE_YD)
        {
            Stop(state);
            return false;
        }
        if (dist < state.bestDist - PROGRESS_YD)
        {
            state.bestDist = dist;
            state.bestMs = now;
        }
        else if (WorldTimer::getMSTimeDiff(state.bestMs, now) > STUCK_MS)
        {
            Stop(state);
            return false;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        float const dx = leader->GetPositionX() - state.goal.x;
        float const dy = leader->GetPositionY() - state.goal.y;
        bool const moved = dx * dx + dy * dy > REPATH_MOVED_YD * REPATH_MOVED_YD;
        if (now >= state.nextMoveMs || moved)
        {
            if (me->GetStandState() != UNIT_STAND_STATE_STAND)
                me->SetStandState(UNIT_STAND_STATE_STAND);
            state.goal.x = leader->GetPositionX();
            state.goal.y = leader->GetPositionY();
            state.goal.z = leader->GetPositionZ();
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, state.goal.x, state.goal.y, state.goal.z,
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
