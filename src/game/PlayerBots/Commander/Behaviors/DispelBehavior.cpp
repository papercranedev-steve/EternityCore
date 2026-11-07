#include "Commander/Behaviors/DispelBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ValueModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellMgr.h"
#include "SpellEntry.h"

namespace Commander
{
    bool DispelBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->Kit().CanDispel();
    }

    bool DispelBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/,
                                   TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Dispel && bot->me->IsInCombat() && !bot->IsInDuel();
    }

    void DispelBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts)
            return;

        for (DispelDuty const& d : plan.dispels)
        {
            if (d.bot != self)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(d.spellId);
            Unit* target = me->GetMap()->GetUnit(d.target);
            if (!spell || !target || !target->IsAlive() || !target->HasAura(d.auraSpellId) ||
                !me->IsWithinLOSInMap(target) || !bot->CanTryToCastSpell(target, spell))
                continue;

            Proposal p;
            p.tier = d.control ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "dispel";
            p.reason = d.control ? "assigned: member cannot act" : "assigned";
            p.action.type = BotActionType::Cast;
            p.action.target = d.target;
            p.action.spell = spell;
            p.SetValue(d.points, Value::LostAction(world, *myFacts, Value::GCD_SEC));
            out.push_back(p);
        }
    }

    bool DispelBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& /*world*/, TeamPlan const& plan) const
    {
        ObjectGuid const self = bot->me->GetObjectGuid();
        for (DispelDuty const& d : plan.dispels)
            if (d.bot == self && d.control)
                return true;
        return false;
    }
}
