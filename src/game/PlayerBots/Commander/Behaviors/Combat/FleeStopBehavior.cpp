#include "Commander/Behaviors/Combat/FleeStopBehavior.h"
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
        bool Runs(Unit const* enemy)
        {
            return enemy->HasUnitState(UNIT_STATE_FLEEING) && !enemy->HasAuraType(SPELL_AURA_MOD_FEAR);
        }
    }

    bool FleeStopBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        for (SpellEntry const* s : bot->Kit().FleeStoppers())
            if (s)
                return true;
        return false;
    }

    void FleeStopBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        FleeStopDuty const* duty = plan.FindFleeStopFor(me->GetObjectGuid());
        if (!duty)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!enemy || !enemy->IsAlive() || !spell)
            return;
        if (enemy->HasUnitState(UNIT_STATE_STUNNED) || enemy->HasUnitState(UNIT_STATE_ROOT))
            return;     // held already (a stun or root of someone else)

        bool const runs = Runs(enemy);
        if (duty->hold)
        {
            if (!runs && enemy->GetHealthPercent() > duty->triggerPct)
                return;
            if (float(enemy->GetHealth()) > Value::DealtDps(world) * duty->holdSec)
                return;     // it would outlive the stun and run afterwards
        }
        else if (!runs)
            return;

        bool const aroundMe = spell->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY;
        Unit* target = aroundMe ? me : enemy;
        if (aroundMe)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            if (!me->IsWithinDistInMap(enemy, radius))
                return;
        }
        else if (!me->IsWithinLOSInMap(enemy))
            return;
        if (!bot->CanCastWithStance(target, spell))
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "fleestop";
        p.reason = duty->hold ? "flee stop: hold it while it dies" : "flee stop: slow the runner";
        p.action.type = BotActionType::Cast;
        p.action.target = target->GetObjectGuid();
        p.action.spell = spell;
        float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }

    bool FleeStopBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const
    {
        FleeStopDuty const* duty = plan.FindFleeStopFor(bot->me->GetObjectGuid());
        if (!duty)
            return false;
        EnemyFacts const* e = world.FindEnemy(duty->enemy);
        return e && (e->flee.fleeing || e->healthPct <= duty->triggerPct + WAKE_MARGIN_PCT);
    }
}
