#include "Commander/Behaviors/Combat/HuntBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Map.h"

namespace Commander
{
    bool HuntBehavior::AppliesTo(TeamBotAI const* /*bot*/) const
    {
        return true;
    }

    void HuntBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (!plan.hunt.Active())
            return;
        Player* me = bot->me;
        if (me->IsInCombat() || bot->IsResting())
            return;
        Unit* victim = me->GetVictim();
        if (victim && victim->GetObjectGuid() == plan.hunt.target)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        Unit* enemy = me->GetMap()->GetUnit(plan.hunt.target);
        if (!enemy || !enemy->IsAlive() || enemy->IsInCombat() || !bot->IsValidHostileTarget(enemy))
            return;

        Proposal p;
        p.source = "hunt";
        p.reason = plan.hunt.why;
        p.tier = ProposalTier::Routine;
        p.action.type = BotActionType::Attack;
        p.action.target = plan.hunt.target;
        p.SetValue(HUNT_VALUE, 0.0f);
        out.push_back(p);
    }
}
