#include "Commander/Behaviors/Support/ResurrectBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
    bool ResurrectBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->Kit().CombatResurrection() != nullptr;
    }

    void ResurrectBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ResurrectDuty const* duty = plan.FindResurrectFor(me->GetObjectGuid());
        if (!duty)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!spell || !bot->IsReadyWithStance(spell))
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "resurrect";
        p.reason = "a dead healer or tank back in the fight";
        p.action.type = BotActionType::Resurrect;
        p.action.spell = spell;
        p.action.target = duty->target;
        float const cost = Value::LostAction(world, *self, std::max(spell->GetCastTime(me) / 1000.0f, Value::GCD_SEC));
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }
}
