#include "Commander/Behaviors/TargetBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/SwitchCost.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Timer.h"

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

        // A suggestion (header): spend the combo points on the current victim first.
        if (duty->suggestUntilMs && int32(duty->suggestUntilMs - WorldTimer::getMSTime()) > 0 &&
            victim && self->comboPoints > 0 && self->comboTarget == victim->GetObjectGuid())
        {
            SpellEntry const* finisher = bot->Kit().ComboFinisher();
            if (finisher && bot->CanTryToCastSpell(victim, finisher))
            {
                Proposal f;
                f.source = "target";
                f.reason = "spend combo points before the switch";
                f.tier = ProposalTier::Assigned;
                f.action.type = BotActionType::Cast;
                f.action.target = victim->GetObjectGuid();
                f.action.spell = finisher;
                f.SetValue(Value::LostAction(world, *self, self->comboPoints * SwitchCost::COMBO_POINT_SEC), 0.0f);
                out.push_back(f);
            }
            return;     // not castable yet (energy): wait until the suggestion ends
        }

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
