#include "Commander/Behaviors/Combat/StoneformBehavior.h"
#include "Commander/Classes/RaceKit.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"

namespace Commander
{
    bool StoneformBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return RaceKit::Stoneform(*bot) != nullptr;
    }

    void StoneformBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct() || !(self->poisoned || self->diseased || self->bleeding))
            return;
        SpellEntry const* spell = RaceKit::Stoneform(*bot);
        if (!spell || !bot->CanTryToCastSpell(me, spell))
            return;

        Proposal p;
        p.tier = ProposalTier::Routine;
        p.source = "stoneform";
        p.reason = "poison, disease or bleed on itself";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        p.action.target = me->GetObjectGuid();
        float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
        p.SetValue(cost + RULE_POINTS, cost);
        out.push_back(p);
    }
}
