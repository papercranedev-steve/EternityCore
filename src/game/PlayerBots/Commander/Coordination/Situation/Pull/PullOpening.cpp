#include "Commander/Coordination/Situation/Pull/PullOpening.h"
#include "Timer.h"

namespace Commander
{
    namespace PullOpening
    {
        void Update(WorldModel const& world, PullPlan& pull, uint32 nowMs)
        {
            bool const holdTimedOut = WorldTimer::getMSTimeDiff(pull.phaseSinceMs, nowMs) > HOLD_MAX_MS;
            pull.holdHealing = pull.losBreak == PullLosBreak::Active && !holdTimedOut && world.HasPartyEnemy();
        }

        void Release(PullPlan& pull)
        {
            pull.holdHealing = false;
        }
    }
}
