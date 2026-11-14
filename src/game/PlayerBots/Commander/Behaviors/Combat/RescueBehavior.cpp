#include "Commander/Behaviors/Combat/RescueBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/RaceKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        bool HasSurvival(PartyBotAI const* bot)
        {
            for (SurvivalSpell const& s : bot->Kit().Survival())
                if (s.spell)
                    return true;
            return !RaceKit::Survival(*bot).empty();
        }
    }

    bool RescueBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        if (HasSurvival(bot))
            return true;
        if (bot->GetRole() == ROLE_TANK)
            return !bot->m_spellListTaunt.empty();
        return bot->Kit().HasThreatDrop();
    }

    bool RescueBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/,
                                   TeamPlan const& /*plan*/) const
    {
        switch (what)
        {
            case Takeover::Taunt:
                return bot->GetRole() == ROLE_TANK && !bot->m_spellListTaunt.empty();
            case Takeover::ThreatDrop:
                return bot->GetRole() != ROLE_TANK && bot->Kit().HasThreatDrop();
            case Takeover::Survival:
                return HasSurvival(bot);
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
        else if (duty->way == RescueWay::Save)
        {
            // On the member about to die, or on itself (Shield Wall, Ice Block, Vanish): a
            // warrior swaps to Defensive Stance first (the arbiter, DoCastWithStance).
            Unit* target = me->GetMap()->GetUnit(duty->victim);
            if (!target || !target->IsAlive() || (target != me && !me->IsWithinLOSInMap(target)) ||
                !bot->CanCastWithStance(target, spell))
                return;
            p.reason = "rescue: about to die, save";
            p.action.target = duty->victim;
        }
        else
        {
            // Feint, Cower: on the enemy hitting it; Fade, Feign Death, Vanish: on itself.
            auto const drop = std::find_if(self->threatDrops.begin(), self->threatDrops.end(),
                                           [&](ThreatDropAbility const& a) { return a.spellId == duty->spellId; });
            if (drop != self->threatDrops.end() && drop->onTarget)
            {
                Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
                if (!enemy || !enemy->IsAlive() || !me->IsWithinLOSInMap(enemy) || !bot->CanTryToCastSpell(enemy, spell))
                    return;
                p.action.target = duty->enemy;
            }
            else
            {
                if (!bot->CanTryToCastSpell(me, spell))
                    return;
                p.action.target = me->GetObjectGuid();
            }
            p.reason = "rescue: attacked, drop threat";
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
