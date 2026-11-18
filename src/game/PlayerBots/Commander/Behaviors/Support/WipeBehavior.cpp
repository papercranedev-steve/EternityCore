#include "Commander/Behaviors/Support/WipeBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "TeamBotAI.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"

namespace Commander
{
    bool WipeBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void WipeBehavior::Propose(TeamBotAI const* bot, WorldModel const& /*world*/,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        WipePlan const& w = plan.wipe;
        if (!w.active)
            return;
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();

        if (w.stoneBy == self && w.stoneItem)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(w.stoneItem);
            SpellEntry const* use = proto ? sSpellMgr.GetSpellEntry(proto->Spells[0].SpellId) : nullptr;
            Unit* target = me->GetMap()->GetUnit(w.stoneOn);
            if (use && target && target->IsAlive() && use->IsTargetInRange(me, target) && me->IsWithinLOSInMap(target))
            {
                Proposal p;
                p.tier = ProposalTier::Urgent;
                p.score = URGENT_SCORE;
                p.source = "wipe";
                p.reason = "the fight is lost: a Soulstone on one who resurrects";
                p.action.type = BotActionType::UseItem;
                p.action.itemId = w.stoneItem;
                p.action.spell = use;
                p.action.target = w.stoneOn;
                out.push_back(p);
            }
            return;
        }

        for (WipeHider const& h : w.hiders)
        {
            if (h.bot != self)
                continue;
            Proposal p;
            p.source = "wipe";
            if (me->HasUnitState(UNIT_STATE_FEIGN_DEATH) || me->HasAuraType(SPELL_AURA_MOD_STEALTH))
            {
                p.tier = ProposalTier::Override;
                p.score = 1.0f;
                p.reason = "the fight is lost: hidden until it is over";
                p.action.type = BotActionType::Hold;
            }
            else
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(h.spellId);
                if (!spell)
                    return;
                p.tier = ProposalTier::Urgent;
                p.score = URGENT_SCORE;
                p.reason = "the fight is lost: drop out of it";
                p.action.type = BotActionType::Cast;
                p.action.spell = spell;
            }
            out.push_back(p);
            return;
        }
    }
}
