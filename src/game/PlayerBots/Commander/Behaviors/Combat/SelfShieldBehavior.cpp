#include "Commander/Behaviors/Combat/SelfShieldBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ShieldValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    bool SelfShieldBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->Kit().SelfShield() != nullptr;
    }

    void SelfShieldBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!world.InFight(me->IsInCombat()))
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        SelfShieldFacts const& s = self->selfShield;
        if (!s.spellId || !s.ready || s.up || s.absorb <= 0.0f)
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(s.spellId);
        if (!spell || !bot->CanTryToCastSpell(me, spell))
            return;

        ShieldValue::Result const v = ShieldValue::Of(world, *self, s.absorb);
        float const cost = Value::LostAction(world, *self, Value::GCD_SEC) +
                           (s.manaCost ? ManaWorth::Points(world, *self, float(s.manaCost)) : 0.0f);
        if (v.benefit <= cost)
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "selfshield";
        p.reason = v.pushbackSec > 0.0f ? "hits coming, casts pushed back: shield up" : "hits coming: shield up";
        p.action.type = BotActionType::Cast;
        p.action.target = self->guid;
        p.action.spell = spell;
        p.SetValue(v.benefit, cost);
        out.push_back(p);
    }
}
