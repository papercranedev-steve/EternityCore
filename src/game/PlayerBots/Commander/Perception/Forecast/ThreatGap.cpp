#include "Commander/Perception/Forecast/ThreatGap.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
namespace ThreatGap
{
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
