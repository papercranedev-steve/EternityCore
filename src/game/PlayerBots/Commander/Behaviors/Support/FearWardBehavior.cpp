#include "Commander/Behaviors/Support/FearWardBehavior.h"
#include "Commander/Classes/RaceKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"

namespace Commander
{
    bool FearWardBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return RaceKit::FearWard(*bot) != nullptr;
    }

    void FearWardBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        FearWardDuty const* duty = plan.fearWards.For(me->GetObjectGuid());
        if (!duty || duty->points <= 0.0f)
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
        p.source = "fear ward";
        p.reason = "a sure fear comes on it";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        p.action.target = duty->target;
        float const cost = Value::LostAction(world, *self, Value::GCD_SEC) +
                           ManaWorth::Points(world, *self, float(Spell::CalculatePowerCost(spell, me)));
        p.SetValue(duty->points, cost);
        out.push_back(p);
    }
}
