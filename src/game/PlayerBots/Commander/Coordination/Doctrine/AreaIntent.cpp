#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace AreaIntent
{
    void Fill(WorldModel const& world, DoctrinePlan& d)
    {
        d.areaThreat.clear();
        d.areaSec = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.CanAct() || (m.role != ROLE_RANGE_DPS && m.role != ROLE_MELEE_DPS))
                continue;
            AreaSpellFacts const* best = nullptr;
            float bestRate = 0.0f;
            for (AreaSpellFacts const& s : m.areaSpells)
            {
                if ((!s.ready && s.spellId != m.areaChannel.spellId) || s.periodSec <= 0.0f)
                    continue;
                float const rate = s.tickThreat / s.periodSec;
                if (rate > bestRate)
                {
                    best = &s;
                    bestRate = rate;
                }
            }
            if (!best)
                continue;
            d.areaThreat.push_back({ m.guid, bestRate });
            d.areaSec = std::max(d.areaSec, float(best->ticks) * best->periodSec);
        }
    }
}
}
