#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
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

    PartyMemberFacts const* PlannedCaster(WorldModel const& world, TeamPlan const& plan)
    {
        DoctrinePlan const& d = plan.doctrine;
        PullPlan const& pull = plan.pull;
        // Before the fight there is no tactic yet; a pull of AREA_MIN or more counts as Area.
        std::vector<PlannedThreat> const casters = d.tactic == PileTactic::Area ? d.areaThreat
            : (pull.phase != PullPhase::None && pull.expected.size() >= TacticalDoctrine::AREA_MIN)
                ? Of(world, nullptr) : std::vector<PlannedThreat>();
        PartyMemberFacts const* m = nullptr;
        float bestRate = 0.0f;
        for (PlannedThreat const& p : casters)
            if (PartyMemberFacts const* f = world.FindMember(p.member))
                if (f->CanAct() && p.perSec > bestRate)
                {
                    m = f;
                    bestRate = p.perSec;
                }
        return m;
    }
}
}
