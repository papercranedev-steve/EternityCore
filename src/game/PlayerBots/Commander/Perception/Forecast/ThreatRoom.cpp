#include "Commander/Perception/Forecast/ThreatRoom.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace ThreatRoom
{
    float OwnRate(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy)
    {
        if (ThreatEntry const* t = ThreatView::FindEntry(enemy, who))
            if (t->threatPerSec > 0.0f)
                return t->threatPerSec;
        float best = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (ThreatEntry const* t = ThreatView::FindEntry(e, who))
                best = std::max(best, t->threatPerSec);
        if (best > 0.0f)
            return best;
        PartyMemberFacts const* me = world.FindMember(who);
        return me ? Value::FALLBACK_DPS_PER_LEVEL * float(me->level) : 0.0f;
    }

    bool Settling(WorldModel const& world, EnemyFacts const& enemy)
    {
        return enemy.firstSeenMs &&
               WorldTimer::getMSTimeDiff(enemy.firstSeenMs, world.refreshedAtMs) < uint32(NEW_ENEMY_SETTLE_SEC * 1000.0f);
    }

    namespace
    {
        float CoverSec(WorldModel const& world, EnemyFacts const& enemy, float tickSec)
        {
            return Settling(world, enemy) ? std::max(tickSec, NEW_ENEMY_LEAD_SEC) : tickSec;
        }
    }

    float Stop(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy, float tickSec)
    {
        float const share = STOP_SHARE * enemy.victimThreat;
        return std::max(share, OwnRate(world, who, enemy) * CoverSec(world, enemy, tickSec));
    }

    float Enter(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy, float tickSec)
    {
        float const share = ENTER_SHARE * enemy.victimThreat;
        float const sec = std::max(ENTER_TICKS * tickSec, CoverSec(world, enemy, tickSec) + tickSec);
        return std::max(share, OwnRate(world, who, enemy) * sec);
    }
}
}
