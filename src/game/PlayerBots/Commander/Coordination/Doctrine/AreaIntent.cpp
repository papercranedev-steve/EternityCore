#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace AreaIntent
{
    std::vector<PlannedThreat> Of(WorldModel const& world, float* sec)
    {
        std::vector<PlannedThreat> out;
        if (sec)
            *sec = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.CanAct() || (m.role != ROLE_RANGE_DPS && m.role != ROLE_MELEE_DPS))
                continue;
            AreaSpellFacts const* best = BestSpell(m);
            if (!best)
                continue;
            out.push_back({ m.guid, best->tickThreat / best->periodSec });
            if (sec)
                *sec = std::max(*sec, float(best->ticks) * best->periodSec);
        }
        return out;
    }

    AreaSpellFacts const* BestSpell(PartyMemberFacts const& m)
    {
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
        return best;
    }

    void Fill(WorldModel const& world, DoctrinePlan& d)
    {
        d.areaThreat = Of(world, &d.areaSec);
    }
}
}
