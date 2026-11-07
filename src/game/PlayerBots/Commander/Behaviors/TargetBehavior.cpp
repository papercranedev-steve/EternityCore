#include "Commander/Behaviors/TargetBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"

namespace Commander
{
    bool TargetBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        CombatBotRoles const role = bot->GetRole();
        return role == ROLE_TANK || role == ROLE_MELEE_DPS || role == ROLE_RANGE_DPS;
    }

    void TargetBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        TargetDuty const* duty = plan.FindTargetFor(me->GetObjectGuid());
        if (!duty)
            return;
        Unit* victim = me->GetVictim();
        if (victim && victim->GetObjectGuid() == duty->enemy)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
        if (!enemy || !enemy->IsAlive() || !bot->IsValidHostileTarget(enemy))
            return;
        if (bot->GetRole() == ROLE_RANGE_DPS &&
            (!me->IsWithinDistInMap(enemy, RANGED_SIGHT_RANGE) || !me->IsWithinLOSInMap(enemy)))
            return;

        Proposal p;
        p.source = "target";
        p.reason = duty->reason;
        p.action.type = BotActionType::Attack;
        p.action.target = duty->enemy;
        if (duty->forced)
            p.tier = ProposalTier::Urgent;
        else
        {
            p.tier = duty->urgent ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.SetValue(duty->benefit, duty->cost);
        }
        out.push_back(p);
    }
}
