#include "Commander/Behaviors/Support/BoostBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    namespace
    {
        bool HasBoosts(PartyBotAI const* bot)
        {
            for (AllyBoostSpell const& s : bot->Kit().AllyBoosts())
                if (s.spell)
                    return true;
            return false;
        }
    }

    bool BoostBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return HasBoosts(bot);
    }

    bool BoostBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/,
                                  TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Boost;
    }

    void BoostBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        BoostDuty const* duty = plan.FindBoostFor(me->GetObjectGuid());
        if (!duty)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        Unit* target = me->GetMap()->GetUnit(duty->target);
        if (!spell || !target || !target->IsAlive() || (target != me && !me->IsWithinLOSInMap(target)) ||
            !bot->CanTryToCastSpell(target, spell))
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "boost";
        p.reason = target == me ? "boost: party mana" : "boost: a party member";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        p.action.target = target->GetObjectGuid();
        float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }
}
