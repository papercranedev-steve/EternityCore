#include "Commander/Coordination/Situation/Guide.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"

namespace Commander
{
namespace Guide
{
    Player* Current(TeamPlan const& plan, Player const* member)
    {
        if (plan.guide.IsEmpty() || !member || !member->IsInWorld() || !Team::HasTeam(member))
            return nullptr;
        Player* guide = member->GetMap()->GetPlayer(plan.guide);
        if (!guide || !guide->IsInWorld() || !guide->IsAlive() || !Team::SameTeam(guide, member))
            return nullptr;
        // Only a party bot: the player is the default anyway, another player is not ours to lead.
        if (!guide->AI() || !dynamic_cast<TeamBotAI const*>(guide->AI()))
            return nullptr;
        return guide;
    }
}
}
