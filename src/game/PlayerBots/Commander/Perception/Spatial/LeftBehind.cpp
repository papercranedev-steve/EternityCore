#include "Commander/Perception/Spatial/LeftBehind.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
namespace LeftBehind
{
    std::vector<LeftBehindMember> Of(Player* anchor, float farYd)
    {
        std::vector<LeftBehindMember> out;
        if (!anchor || !anchor->IsInWorld())
            return out;
        for (Player* member : Team::Members(anchor))
        {
            if (!member || member == anchor || !member->IsInWorld() || member->IsBeingTeleported() ||
                !member->IsAlive() || member->IsInCombat() || member->IsTaxiFlying())
                continue;
            TeamBotAI const* ai = member->AI() ? dynamic_cast<TeamBotAI const*>(member->AI()) : nullptr;
            if (!ai)
                continue;
            StaySpot spot;
            if (sCommanderMgr.GetStay(ai, spot))
                continue;
            LeftBehindMember s;
            s.member = member;
            s.otherMap = member->GetMapId() != anchor->GetMapId() || member->GetInstanceId() != anchor->GetInstanceId();
            if (!s.otherMap)
            {
                s.distance = member->GetDistance(anchor, SizeFactor::None);
                if (s.distance <= farYd)
                    continue;
            }
            s.rank = ai->GetRole() == ROLE_TANK ? 0 : ai->HealsForTeam() ? 1 : 2;
            out.push_back(s);
        }
        std::sort(out.begin(), out.end(), [](LeftBehindMember const& a, LeftBehindMember const& b)
        {
            if (a.rank != b.rank)
                return a.rank < b.rank;
            if (a.otherMap != b.otherMap)
                return a.otherMap;
            return a.distance > b.distance;
        });
        return out;
    }
}
}
