#include "Commander/Coordination/Situation/Guide.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Player.h"

namespace Commander
{
namespace Guide
{
    Player* Current(TeamPlan const& plan, Player const* member)
    {
        if (plan.guide.IsEmpty() || !member || !member->IsInWorld() || !member->GetGroup())
            return nullptr;
        Player* guide = member->GetMap()->GetPlayer(plan.guide);
        if (!guide || !guide->IsInWorld() || !guide->IsAlive() || guide->GetGroup() != member->GetGroup())
            return nullptr;
        // Only a party bot: the player is the default anyway, another player is not ours to lead.
        if (!guide->AI() || !dynamic_cast<PartyBotAI const*>(guide->AI()))
            return nullptr;
        return guide;
    }
}
}
