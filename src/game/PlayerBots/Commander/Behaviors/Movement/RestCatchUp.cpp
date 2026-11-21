#include "Commander/Behaviors/Movement/RestCatchUp.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "MoveSpline.h"
#include "Timer.h"

namespace Commander
{
namespace RestCatchUp
{
    bool Update(TeamBotAI const* bot, RestCatchUpState& state, Unit const* leader)
    {
        Player const* me = bot->me;
        if (!leader || me->IsInCombat() || !me->IsInMap(leader))
        {
            state = RestCatchUpState();
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        bool const moving = leader->IsMoving() || !leader->movespline->Finalized();
        if (moving)
            state.stillSinceMs = 0;
        else if (!state.stillSinceMs)
            state.stillSinceMs = now ? now : 1;

        float const dist = me->GetDistance(leader);
        if (state.active)
        {
            bool const waits = state.stillSinceMs && WorldTimer::getMSTimeDiff(state.stillSinceMs, now) >= STILL_MS;
            if (dist <= BACK_YD && waits)
                state.active = false;
            return state.active;
        }

        bool const leaving = (dist > LEAVING_YD && moving) || dist > FAR_YD;
        if (leaving && (!bot->IsResting() || bot->RestSecondsLeft() > FINISH_SEC))
            state.active = true;
        return state.active;
    }
}
}
