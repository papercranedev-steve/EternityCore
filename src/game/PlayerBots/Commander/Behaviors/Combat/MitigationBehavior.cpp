#include "Commander/Behaviors/Combat/MitigationBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/MitigationValue.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    bool MitigationBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        if (bot->GetRole() != ROLE_TANK)
            return false;
        for (SpellEntry const* spell : bot->Kit().AttackPowerDebuffs())
            if (spell)
                return true;
        return false;
    }

    void MitigationBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!me->IsInCombat())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        for (MitigationAbility const& a : self->mitigations)
        {
            if (!a.ready || MitigationValue::PullsIdle(world, a, self->pos))
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
            if (!spell || !bot->CanCastWithStance(me, spell))
                continue;
            float const benefit = MitigationValue::Benefit(world, a, self->pos);
            if (benefit <= 0.0f)
                continue;

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "mitigation";
            p.reason = "attack power debuff on the enemies around";
            p.action.type = BotActionType::Cast;
            p.action.spell = spell;
            p.action.target = ObjectGuid();     // self: an area around the caster
            p.SetValue(benefit, MitigationValue::Cost(world, *self, a));
            out.push_back(p);
        }
    }
}
