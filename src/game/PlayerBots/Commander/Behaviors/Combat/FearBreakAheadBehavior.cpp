#include "Commander/Behaviors/Combat/FearBreakAheadBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Forecast/IncomingTimeline.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Seconds of fear immunity the spell gives its caster, 0 = none.
        float FearImmunitySec(SpellEntry const* spell)
        {
            if (!spell)
                return 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                    spell->EffectApplyAuraName[i] == SPELL_AURA_MECHANIC_IMMUNITY &&
                    spell->EffectMiscValue[i] == MECHANIC_FEAR)
                    return std::max(0, spell->GetDuration()) / 1000.0f;
            return 0.0f;
        }
    }

    bool FearBreakAheadBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return FearImmunitySec(bot->Kit().FearBreak()) > 0.0f;
    }

    void FearBreakAheadBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                         TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        SpellEntry const* spell = bot->Kit().FearBreak();
        float const immuneSec = FearImmunitySec(spell);
        if (immuneSec <= 0.0f)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        // Its next fear (a member already immune has no fear lockout).
        IncomingTimeline::Lockout const* fear = nullptr;
        IncomingTimeline::Timeline const timeline = IncomingTimeline::Of(world, *self);
        for (IncomingTimeline::Lockout const& l : timeline.lockouts)
            if (l.Fear() && l.startSec >= LEAD_SEC)
            {
                fear = &l;
                break;
            }
        if (!fear || fear->startSec > immuneSec)
            return;
        // In its stance only (CanTryToCastSpell checks the form).
        if (!bot->CanTryToCastSpell(me, spell))
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "fear break ahead";
        p.reason = "a sure fear comes on it";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        p.action.target = me->GetObjectGuid();
        float const saved = Value::LostAction(world, *self, fear->endSec - fear->startSec);
        p.SetValue(saved, Value::LostAction(world, *self, Value::GCD_SEC));
        out.push_back(p);
    }
}
