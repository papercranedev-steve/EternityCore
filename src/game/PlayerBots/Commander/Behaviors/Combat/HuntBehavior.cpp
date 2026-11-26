#include "Commander/Behaviors/Combat/HuntBehavior.h"
#include "Commander/Classes/ClassKit.h"
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
        p.SetValue(HUNT_VALUE, 0.0f);

        Unit* victim = me->GetVictim();
        if (!victim || victim->GetObjectGuid() != plan.hunt.target)
        {
            p.action.type = BotActionType::Attack;
            p.action.target = plan.hunt.target;
            out.push_back(p);
            return;
        }

        // On its way to the creature: the opener goes into stealth near it (after the Attack,
        // so the rotation sees a victim and keeps the stealth: ClassRotation::DropUnwantedStealth).
        SpellEntry const* stealth = bot->Kit().Stealth();
        if (!stealth || plan.hunt.opener != self->guid || me->HasAuraType(SPELL_AURA_MOD_STEALTH))
            return;
        float const dist = me->GetCombatDistance(enemy);
        if (dist > STEALTH_FROM_YD || dist <= STEALTH_MIN_YD || !bot->CanTryToCastSpell(me, stealth))
            return;
        p.reason = "opener: stealth";
        p.action.type = BotActionType::Cast;
        p.action.spell = stealth;
        p.action.target = self->guid;
        out.push_back(p);
    }
}
