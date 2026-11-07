#include "Commander/Coordination/PullOpening.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ThreatView.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace PullOpening
    {
        namespace
        {
            bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid g)
            {
                return std::find(list.begin(), list.end(), g) != list.end();
            }

            bool OnTank(EnemyFacts const& e, PullPlan const& pull)
            {
                return e.acting && !e.crowdControlled && !e.onAllyOnly && e.victim == pull.tank;
            }
        }

        void Update(WorldModel const& world, PullPlan& pull, Position3 const& tankAt, uint32 nowMs)
        {
            bool reached = false;       // a pulled enemy is next to the tank
            bool coming = false;        // one walks to the tank with melee on
            bool held = true;           // the tank has threat on each one next to it
            for (EnemyFacts const& e : world.enemies)
            {
                if (!Contains(pull.expected, e.guid) || !OnTank(e, pull))
                    continue;
                float const gap = PackResolver::Distance(e.pos, tankAt) - e.boundingRadius;
                if (gap > THREAT_REACH)
                {
                    coming = coming || e.meleeEnabled;
                    continue;
                }
                reached = true;
                ThreatEntry const* own = ThreatView::FindEntry(e, pull.tank);
                if (!own || own->threat <= 0.0f)
                    held = false;
            }
            if (reached && !pull.threatSinceMs)
                pull.threatSinceMs = nowMs;

            bool const threatTimedOut = pull.threatSinceMs &&
                WorldTimer::getMSTimeDiff(pull.threatSinceMs, nowMs) > THREAT_TIMEOUT_MS;
            bool const holdTimedOut = WorldTimer::getMSTimeDiff(pull.phaseSinceMs, nowMs) > HOLD_MAX_MS;
            bool const nothingToWaitFor = !reached && !coming;

            pull.holdDamage = world.HasPartyEnemy() && !nothingToWaitFor && !(reached && held) &&
                              !threatTimedOut && !holdTimedOut;
            pull.holdHealing = pull.holdDamage ||
                               (pull.losBreak == PullLosBreak::Active && !holdTimedOut && world.HasPartyEnemy());
        }

        void Release(PullPlan& pull)
        {
            pull.holdDamage = false;
            pull.holdHealing = false;
            pull.threatSinceMs = 0;
        }
    }
}
