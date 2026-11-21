#include "Commander/Coordination/Situation/Stay.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Team.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace Stay
{
    bool Begin(TeamPlan& plan, Player const* member)
    {
        if (!member || !member->IsInWorld())
            return false;
        End(plan, member);
        StaySpot spot;
        spot.member = member->GetObjectGuid();
        spot.mapId = member->GetMapId();
        spot.x = member->GetPositionX();
        spot.y = member->GetPositionY();
        spot.z = member->GetPositionZ();
        spot.o = member->GetOrientation();
        spot.sinceMs = WorldTimer::getMSTime();
        plan.stay.members.push_back(spot);
        return true;
    }

    bool End(TeamPlan& plan, Player const* member)
    {
        if (!member)
            return false;
        std::vector<StaySpot>& members = plan.stay.members;
        auto const itr = std::remove_if(members.begin(), members.end(),
            [member](StaySpot const& s) { return s.member == member->GetObjectGuid(); });
        bool const was = itr != members.end();
        members.erase(itr, members.end());
        return was;
    }

    StaySpot const* Current(TeamPlan const& plan, Player const* member)
    {
        if (!member || !member->IsInWorld() || !Team::HasTeam(member))
            return nullptr;
        StaySpot const* spot = plan.stay.Find(member->GetObjectGuid());
        return spot && spot->mapId == member->GetMapId() ? spot : nullptr;
    }
}
}
