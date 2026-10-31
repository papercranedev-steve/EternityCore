#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "Map.h"

namespace Commander
{
    namespace
    {
        Proposal Make(ProposalTier tier, float score, BotActionType type, ObjectGuid target = ObjectGuid())
        {
            Proposal p;
            p.tier = tier;
            p.score = score;
            p.source = "pull";
            p.action.type = type;
            p.action.target = target;
            return p;
        }
    }

    bool PullBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void PullBehavior::Propose(PartyBotAI const* bot, WorldModel const& /*world*/,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        PullPlan const& pull = plan.pull;
        if (pull.phase == PullPhase::None || pull.phase == PullPhase::WaitReady)
            return;

        Player* me = bot->me;

        if (me->GetObjectGuid() == pull.tank)
        {
            if (pull.phase == PullPhase::Approach)
            {
                Unit* target = me->GetMap()->GetUnit(pull.target);
                if (target && target->IsAlive() && me->GetVictim() != target)
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Attack, pull.target));
            }
            else if (pull.phase == PullPhase::Return && !bot->IsMovementHeld())
            {
                Position3 here;
                here.x = me->GetPositionX();
                here.y = me->GetPositionY();
                here.z = me->GetPositionZ();
                float const distance = PackResolver::Distance(here, pull.spot);
                Proposal p = Make(ProposalTier::Assigned, 5.0f, BotActionType::MoveTo);
                p.action.x = pull.spot.x;
                p.action.y = pull.spot.y;
                p.action.z = pull.spot.z;
                p.action.holdMs = uint32(distance / RUN_SPEED_YD_PER_SEC * 1000.0f) + 500;
                out.push_back(p);
            }
            return;
        }

        // Everyone else waits for the tank. Higher-scored proposals (interrupts, threat) still win.
        if (bot->GetRole() == ROLE_HEALER && me->IsInCombat())
            return;
        out.push_back(Make(ProposalTier::Assigned, 1.0f, BotActionType::Hold));
    }
}
