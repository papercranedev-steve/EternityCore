#include "Commander/Behaviors/Support/PurgeBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "SpellMgr.h"
#include "SpellEntry.h"

namespace Commander
{
    bool PurgeBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        for (SpellEntry const* s : bot->Kit().Purges())
            if (s)
                return true;
        return false;
    }

    void PurgeBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts || !myFacts->CanAct())
            return;

        for (PurgeDuty const& d : plan.purges)
        {
            if (d.bot != self)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(d.spellId);
            Unit* enemy = me->GetMap()->GetUnit(d.enemy);
            if (!spell || !enemy || !enemy->IsAlive() || !enemy->HasAura(d.auraSpellId) ||
                !me->IsWithinLOSInMap(enemy) || !bot->CanTryToCastSpell(enemy, spell))
                continue;

            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "purge";
            p.reason = "assigned: take its buff off";
            p.action.type = BotActionType::Cast;
            p.action.target = d.enemy;
            p.action.spell = spell;
            p.SetValue(d.points, Value::LostAction(world, *myFacts, Value::GCD_SEC));
            out.push_back(p);
        }
    }
}
