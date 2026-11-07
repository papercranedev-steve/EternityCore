#include "Commander/Behaviors/CrowdControlBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    bool CrowdControlBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        for (SpellEntry const* s : bot->Kit().Controls())
            if (s)
                return true;
        return false;
    }

    bool CrowdControlBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/) const
    {
        return what == Takeover::CrowdControl && bot->me->IsInCombat() && !bot->IsInDuel();
    }

    void CrowdControlBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ControlDuty const* duty = plan.FindControlFor(me->GetObjectGuid());
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!duty || !self || !self->CanAct() || !me->IsInCombat())
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!spell)
            return;

        // Psychic Scream and Howl of Terror are cast on the bot itself; Intimidating Shout and
        // every single target spell on the enemy (the shout's target cowers, the rest runs).
        Unit* target = nullptr;
        if (spell->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY)
        {
            target = me->GetMap()->GetUnit(duty->enemy);
            if (!target || !target->IsAlive() || (!duty->area && target->HasAura(spell->Id)) ||
                !me->IsWithinLOSInMap(target) || !bot->CanTryToCastSpell(target, spell))
                return;
        }
        else if (!bot->CanTryToCastSpell(me, spell))
            return;

        float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "cc";
        p.reason = duty->area ? "assigned: area control" : "assigned: control";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        if (target)
            p.action.target = target->GetObjectGuid();
        p.action.dropTarget = duty->breaksOnDamage;
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }
}
