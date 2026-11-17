#include "Commander/Behaviors/Movement/PushBehavior.h"
#include "Commander/BotDirectives.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Commander
{
    namespace
    {
        char const* const SOURCE = "push";

        Proposal Make(BotActionType type, char const* reason)
        {
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.score = 5.0f;
            p.source = SOURCE;
            p.reason = reason;
            p.action.type = type;
            return p;
        }
    }

    bool PushBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void PushBehavior::Propose(PartyBotAI const* bot, WorldModel const& /*world*/,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        PushPlan const& push = plan.push;
        Player* me = bot->me;
        if (!push.active || plan.guide != me->GetObjectGuid())
            return;

        if (!push.walking || me->IsInCombat())
        {
            // The leg ended (a fight, the planner stopped it): our own walk only.
            char const* owner = bot->Directives().GetMovementOwner(WorldTimer::getMSTime());
            if (owner && !strcmp(owner, SOURCE))
                out.push_back(Make(BotActionType::StopMove, "push: leg over"));
            return;
        }

        // On the way: do not restart the spline.
        if (me->IsMoving() && bot->IsMovementHeld())
        {
            out.push_back(Make(BotActionType::Hold, "push: walking the leg"));
            return;
        }

        float const dx = push.legEnd.x - me->GetPositionX();
        float const dy = push.legEnd.y - me->GetPositionY();
        float const dz = push.legEnd.z - me->GetPositionZ();
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        Proposal p = Make(BotActionType::MoveTo, "push: next leg");
        p.action.x = push.legEnd.x;
        p.action.y = push.legEnd.y;
        p.action.z = push.legEnd.z;
        p.action.holdMs = uint32(distance * PATH_FACTOR / speed * 1000.0f) + HOLD_EXTRA_MS;
        out.push_back(p);
    }
}
