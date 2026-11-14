#include "Commander/Behaviors/Combat/BurstBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/RaceKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/BurstValue.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Pet.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    namespace
    {
        bool HasBursts(PartyBotAI const* bot)
        {
            for (BurstSpell const& s : bot->Kit().Bursts())
                if (s.spell)
                    return true;
            return !RaceKit::Bursts(*bot).empty();
        }
    }

    bool BurstBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_HEALER && HasBursts(bot);
    }

    bool BurstBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/,
                                  TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Burst;
    }

    void BurstBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!world.InFight(me->IsInCombat()))
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct() || self->bursts.empty())
            return;
        Unit* victimUnit = me->GetVictim();
        EnemyFacts const* victim = victimUnit ? world.FindEnemy(victimUnit->GetObjectGuid()) : nullptr;
        bool const noHold = victim && plan.doctrine.Has(victim->guid, STAMP_NO_HOLD);
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;

        for (BurstAbility const& a : self->bursts)
        {
            BurstValue::Option const o = BurstValue::Of(world, *self, a, victim, noHold, tickSec);
            if (!o.ok)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
            if (!spell)
                continue;

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "burst";
            p.reason = o.why;
            p.action.type = BotActionType::Cast;
            if (a.thenSpellId)
            {
                // The boosted cast must go now, right after the cooldown.
                SpellEntry const* then = sSpellMgr.GetSpellEntry(a.thenSpellId);
                if (!then || !victimUnit || !me->IsWithinLOSInMap(victimUnit) ||
                    !bot->CanTryToCastSpell(victimUnit, then))
                    continue;
                if (a.thenNotOnTarget && victimUnit->HasAura(then->Id))
                    continue;
                p.action.spell = then;
                p.action.target = victimUnit->GetObjectGuid();
                p.action.prepSpell = spell;
            }
            else if (a.needsPet)
            {
                Pet* pet = me->GetPet();
                if (!pet || !pet->IsAlive() || !bot->CanTryToCastSpell(pet, spell))
                    continue;
                p.action.spell = spell;
                p.action.target = pet->GetObjectGuid();
            }
            else
            {
                if (!bot->CanCastWithStance(me, spell))
                    continue;
                p.action.spell = spell;
                p.action.target = me->GetObjectGuid();
            }
            p.SetValue(o.benefit, o.cost);
            out.push_back(p);
        }
    }
}
