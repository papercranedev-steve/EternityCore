#include "Commander/Perception/Forecast/TauntWindow.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>

namespace Commander
{
namespace TauntWindow
{
    Window Of(EnemyFacts const& e, PartyMemberFacts const& victim, PartyMemberFacts const& tank,
              TauntAbility const& taunt)
    {
        Window w;
        w.arriveSec = e.rooted ? ThreatView::NO_LIMIT : Runners::ArriveSec(e, victim.pos, nullptr, victim.pos);

        float const gap = PackResolver::Distance(tank.pos, e.pos) - e.boundingRadius;
        if (gap > taunt.maxRange)
            w.reachLeftSec = 0.0f;
        else
        {
            float const apart = e.speed - TANK_RUN_SPEED;
            w.reachLeftSec = e.rooted || apart <= 0.0f ? ThreatView::NO_LIMIT
                                                       : std::max(0.0f, taunt.maxRange - gap) / apart;
        }

        w.threatNow = e.victimThreat;
        if (ThreatEntry const* row = ThreatView::FindEntry(e, victim.guid))
            w.threatPerSec = std::max(0.0f, row->threatPerSec);
        if (victim.pendingHit.target == e.guid)
        {
            w.hitThreat = victim.pendingHit.threat;
            w.hitLandSec = victim.pendingHit.landMs / 1000.0f;
        }
        return w;
    }

    float ThreatGain(Window const& w, float sec)
    {
        float const rate = w.threatPerSec * std::max(0.0f, sec);
        float const hit = w.hitThreat > 0.0f && w.hitLandSec <= sec ? w.hitThreat : 0.0f;
        return std::max(rate, hit);
    }
}
}
