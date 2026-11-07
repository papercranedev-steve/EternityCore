#include "Commander/Behaviors/RescueBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    bool RescueBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        if (bot->GetRole() == ROLE_TANK)
            return !bot->m_spellListTaunt.empty();
        return bot->Kit().ThreatDrop() != nullptr;
    }

    bool RescueBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/) const
    {
        if (bot->IsInDuel())
            return false;
        switch (what)
        {
            case Takeover::Taunt:
                return bot->GetRole() == ROLE_TANK && !bot->m_spellListTaunt.empty();
            case Takeover::ThreatDrop:
                return bot->GetRole() != ROLE_TANK && bot->Kit().ThreatDrop();
            default:
                return false;
        }
    }

    void RescueBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        RescueDuty const* duty = plan.FindRescueFor(me->GetObjectGuid());
        if (!duty || duty->way == RescueWay::Threat)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!spell)
            return;

        Proposal p;
        p.tier = duty->urgent ? ProposalTier::Urgent : ProposalTier::Assigned;
        p.source = "rescue";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        if (duty->way == RescueWay::Taunt)
        {
            Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
            if (!enemy || !enemy->IsAlive() || !me->IsWithinLOSInMap(enemy) || !bot->CanCastWithStance(enemy, spell))
                return;
            p.reason = "rescue: taunt loose enemy";
            p.action.target = duty->enemy;
        }
        else
        {
            if (!bot->CanTryToCastSpell(me, spell))
                return;
            p.reason = "rescue: attacked, fade";
            p.action.target = me->GetObjectGuid();
        }
        float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }

    bool RescueBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const
    {
        RescueDuty const* duty = plan.FindRescueFor(bot->me->GetObjectGuid());
        // Also for Threat: TargetBehavior switches the tank's target on that tick.
        return world.HasPartyEnemy() && duty;
    }
}
