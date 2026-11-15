#include "Commander/Behaviors/Combat/RenewSurvivalBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "SpellMgr.h"

namespace Commander
{
    bool RenewSurvivalBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        for (SurvivalSpell const& s : bot->Kit().Survival())
            if (s.spell && s.renewWhileHit && bot->Kit().CooldownReset(s.spell))
                return true;
        return false;
    }

    void RenewSurvivalBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                        TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!me->IsInCombat())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        for (SurvivalAbility const& a : self->survival)
        {
            if (!a.renewWhileHit || !a.ready || !a.reset.spellId)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
            SpellEntry const* reset = sSpellMgr.GetSpellEntry(a.reset.spellId);
            if (!spell || !reset || me->HasAura(spell->Id))
                continue;
            // Ended within the window: cooldown left >= cooldown - duration - window.
            int32 const left = me->GetSpellCooldownLeft(spell);
            int32 const endedAt = int32(a.cooldownMs) - int32(a.durationSec * 1000.0f);
            if (left <= 0 || left > endedAt || left < endedAt - int32(RENEW_WINDOW_MS))
                continue;
            SurvivalValue::Danger const danger = SurvivalValue::Of(world, *self);
            if (danger.physicalDps <= 0.0f)
                continue;
            if (!bot->CanTryToCastSpell(me, reset))
                continue;

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "renew";
            p.reason = "still hit in melee as it ran out: reset and renew";
            p.action.type = BotActionType::Cast;
            p.action.spell = spell;
            p.action.target = me->GetObjectGuid();
            p.action.prepSpell = reset;
            p.action.prepResets = true;
            float const kept = danger.physicalDps * (1.0f - a.amount) * a.durationSec;
            float const cost = a.reset.gcd || a.gcd ? Value::LostAction(world, *self, Value::GCD_SEC) : 0.0f;
            p.SetValue(Value::Points(world, kept) + cost, cost);
            out.push_back(p);
        }
    }
}
