#include "Commander/Perception/Forecast/ThreatGap.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
namespace ThreatGap
{
    namespace
    {
        // Enemies the heal's threat is split over: those with the tank on their list.
        uint32 SplitCount(WorldModel const& world, ObjectGuid tank)
        {
            uint32 n = 0;
            for (EnemyFacts const& e : world.enemies)
                if (ThreatEntry const* t = ThreatView::FindEntry(e, tank))
                    if (t->onList)
                        ++n;
            return std::max<uint32>(1, n);
        }

        float HealThreatOf(PartyMemberFacts const& m, uint32 split)
        {
            return m.isBot && !m.isPet && m.CanAct() && m.role == ROLE_HEALER ? m.healThreat / float(split) : 0.0f;
        }
    }

    float NextHealThreat(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank)
    {
        if (e.ignoresAssistThreat)
            return 0.0f;
        uint32 const split = SplitCount(world, tank);
        float best = 0.0f;
        for (PartyMemberFacts const& m : world.party)
            best = std::max(best, HealThreatOf(m, split));
        return best;
    }

    float Of(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank)
    {
        uint32 const split = SplitCount(world, tank);
        float needed = 0.0f;
        float own = 0.0f;
        for (ThreatEntry const& t : e.threat)
        {
            if (t.guid == tank)
            {
                own = t.threat;
                continue;
            }
            if (!t.partySide)
                continue;
            float threat = t.onList ? t.threat : 0.0f;
            if (!e.ignoresAssistThreat)
                if (PartyMemberFacts const* m = world.FindMember(t.guid))
                    threat += HealThreatOf(*m, split);
            float const factor = t.inMeleeReach ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
            needed = std::max(needed, threat / factor);
        }
        // Healer bots not on the list (no entry at all): the heal alone.
        if (!e.ignoresAssistThreat)
            for (PartyMemberFacts const& m : world.party)
                if (!ThreatView::FindEntry(e, m.guid))
                    needed = std::max(needed, HealThreatOf(m, split) / ThreatView::RANGED_AGGRO_FACTOR);
        float gap = std::max(0.0f, needed - own);
        if (own <= 0.0f)
            gap = std::max(gap, 1.0f);
        return gap;
    }

    float FillRate(WorldModel const& world, PartyMemberFacts const& tank)
    {
        float const measured = LooseEnemy::TankRate(world, tank.guid);
        if (measured > 1.0f)
            return measured;
        return std::max(1.0f, tank.soloDps * tank.threatMod);
    }

    float WaitCostPerSec(WorldModel const& world, ObjectGuid tank)
    {
        float cost = 0.0f;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && !m.isPet && m.CanAct() && m.guid != tank && m.role != ROLE_HEALER && !Tanks::IsTank(m))
                cost += Value::ContributionPerSec(world, m);
        return cost;
    }
}
}
