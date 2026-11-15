#include "Commander/Behaviors/Combat/RescueBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/RaceKit.h"
#include "Commander/Classes/ItemKit.h"
#include "Commander/Coordination/Duties/RescueAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Pet.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        bool HasSurvival(PartyBotAI const* bot)
        {
            for (SurvivalSpell const& s : bot->Kit().Survival())
                if (s.spell)
                    return true;
            return !RaceKit::Survival(*bot).empty() || ItemKit::HoldsAny(*bot, ItemKit::Use::Heal);
        }

        // The duty's spell is on cooldown, its reset ready (CooldownResetFacts): cast the reset
        // first (BotAction::prepResets; the arbiter checks the spell after it).
        bool SetReset(PartyBotAI const* bot, BotAction& action, CooldownResetFacts const& reset)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(reset.spellId);
            if (!spell || bot->me->IsSpellReady(action.spell) || !bot->CanTryToCastSpell(bot->me, spell))
                return false;
            action.prepSpell = spell;
            action.prepResets = true;
            return true;
        }

        // A waiting taunt (RescueDuty::waitUntilMs, RescueAssigner::TauntWaitSec) is due: its
        // time is up, or, checked live, within RescueAssigner::WAIT_LEAD_SEC the enemy is on its
        // victim or out of the tank's reach (`reach`: TauntAbility::maxRange, from its edge).
        bool TauntDue(PartyBotAI const* bot, RescueDuty const& duty, Unit const* enemy, float reach)
        {
            if (!duty.waitUntilMs || int32(WorldTimer::getMSTime() - duty.waitUntilMs) >= 0)
                return true;
            Player* me = bot->me;
            Unit const* victim = me->GetMap()->GetUnit(duty.victim);
            if (!victim || !victim->IsAlive() || enemy->GetVictim() != victim ||
                enemy->CanReachWithMeleeAutoAttack(victim))
                return true;
            float const lead = RescueAssigner::WAIT_LEAD_SEC;
            float const speed = enemy->GetSpeed(MOVE_RUN);
            if (enemy->GetDistance(victim) <= RescueValue::MELEE_REACH + speed * lead)
                return true;
            float const dx = me->GetPositionX() - enemy->GetPositionX();
            float const dy = me->GetPositionY() - enemy->GetPositionY();
            float const gap = std::sqrt(dx * dx + dy * dy) - enemy->GetObjectBoundingRadius();
            float const apart = std::max(0.0f, speed - me->GetSpeed(MOVE_RUN));
            return gap + apart * lead >= reach;
        }
    }

    bool RescueBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        if (HasSurvival(bot))
            return true;
        if (bot->GetRole() == ROLE_TANK)
            return !bot->m_spellListTaunt.empty();
        return bot->Kit().HasThreatDrop();
    }

    // The brain gave the bot's pet a rescue of its own spells (WorldModelBuilder's pet facts):
    // Shell Shield on itself about to die, Cower on the enemy hitting it. The pet casts it
    // (BotActionType::PetCast), the bot's own action is not taken.
    void RescueBehavior::ProposePet(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Pet* pet = bot->me->GetPet();
        if (!pet || !pet->IsAlive() || pet->IsCharmed())
            return;
        RescueDuty const* duty = plan.FindRescueFor(pet->GetObjectGuid());
        if (!duty || (duty->way != RescueWay::Save && duty->way != RescueWay::ThreatDrop))
            return;
        PartyMemberFacts const* self = world.FindMember(pet->GetObjectGuid());
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!self || !self->CanAct() || !spell || !pet->HasSpell(spell->Id) || !pet->IsSpellReady(spell))
            return;

        Proposal p;
        p.tier = duty->urgent ? ProposalTier::Urgent : ProposalTier::Assigned;
        p.source = "rescue";
        p.action.type = BotActionType::PetCast;
        p.action.spell = spell;
        if (duty->way == RescueWay::ThreatDrop)
        {
            Unit* enemy = pet->GetMap()->GetUnit(duty->enemy);
            if (!enemy || !enemy->IsAlive() || !pet->IsWithinLOSInMap(enemy))
                return;
            p.action.target = duty->enemy;
            p.reason = "rescue: pet attacked, drop threat";
        }
        else
            p.reason = "rescue: pet about to die, save";
        float const cost = spell->StartRecoveryTime > 0 ? Value::LostAction(world, *self, Value::GCD_SEC) : 0.0f;
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }

    void RescueBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        ProposePet(bot, world, plan, out);
        Player* me = bot->me;
        RescueDuty const* duty = plan.FindRescueFor(me->GetObjectGuid());
        if (!duty || duty->way == RescueWay::Threat)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!spell)
            return;

        Proposal p;
        p.tier = duty->urgent ? ProposalTier::Urgent : ProposalTier::Assigned;
        p.source = "rescue";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        bool const gcd = spell->StartRecoveryCategory > 0;
        if (duty->way == RescueWay::Save)
        {
            // A healthstone or healing potion (ItemKit): its spell is the duty's.
            auto const item = std::find_if(self->survival.begin(), self->survival.end(),
                                           [&](SurvivalAbility const& a) { return a.itemId && a.spellId == duty->spellId; });
            if (item != self->survival.end())
            {
                if (!item->ready || duty->victim != me->GetObjectGuid())
                    return;
                p.reason = "rescue: about to die, use an item";
                p.action.type = BotActionType::UseItem;
                p.action.itemId = item->itemId;
                p.action.target = duty->victim;
                float const cost = item->gcd ? Value::LostAction(world, *self, Value::GCD_SEC) : 0.0f;
                p.SetValue(duty->points + cost, cost);
                out.push_back(p);
                return;
            }
        }
        if (duty->way == RescueWay::Taunt)
        {
            Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
            if (!enemy || !enemy->IsAlive() || !me->IsWithinLOSInMap(enemy) || !bot->CanCastWithStance(enemy, spell))
                return;
            // A waiting taunt: the tank keeps after it (TankTargets) and fights until it is due.
            if (duty->waitUntilMs)
            {
                auto const taunt = std::find_if(self->taunts.begin(), self->taunts.end(),
                                                [&](TauntAbility const& t) { return t.spellId == duty->spellId; });
                if (taunt != self->taunts.end() && !TauntDue(bot, *duty, enemy, taunt->maxRange))
                    return;
            }
            p.reason = duty->waitUntilMs ? "rescue: taunt loose enemy after its victim's threat" : "rescue: taunt loose enemy";
            p.action.target = duty->enemy;
        }
        else if (duty->way == RescueWay::Save)
        {
            // On the member about to die, or on itself (Shield Wall, Ice Block, Vanish): a
            // warrior swaps to Defensive Stance first (the arbiter, DoCastWithStance).
            Unit* target = me->GetMap()->GetUnit(duty->victim);
            if (!target || !target->IsAlive() || (target != me && !me->IsWithinLOSInMap(target)))
                return;
            auto const own = std::find_if(self->survival.begin(), self->survival.end(),
                                          [&](SurvivalAbility const& a) { return !a.itemId && a.spellId == duty->spellId; });
            // Facts a tick old may still ask for the reset just cast: then the spell itself.
            bool const reset = own != self->survival.end() && own->reset.spellId && SetReset(bot, p.action, own->reset);
            if (!reset && !bot->CanCastWithStance(target, spell))
                return;
            p.reason = p.action.prepResets ? "rescue: about to die, reset and save" : "rescue: about to die, save";
            p.action.target = duty->victim;
        }
        else
        {
            // Feint, Cower: on the enemy hitting it; Fade, Feign Death, Vanish: on itself.
            auto const drop = std::find_if(self->threatDrops.begin(), self->threatDrops.end(),
                                           [&](ThreatDropAbility const& a) { return a.spellId == duty->spellId; });
            if (drop != self->threatDrops.end() && drop->onTarget)
            {
                Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
                if (!enemy || !enemy->IsAlive() || !me->IsWithinLOSInMap(enemy) || !bot->CanTryToCastSpell(enemy, spell))
                    return;
                p.action.target = duty->enemy;
            }
            else
            {
                bool const reset = drop != self->threatDrops.end() && drop->reset.spellId &&
                                   SetReset(bot, p.action, drop->reset);
                if (!reset && !bot->CanTryToCastSpell(me, spell))
                    return;
                p.action.target = me->GetObjectGuid();
            }
            p.reason = p.action.prepResets ? "rescue: attacked, reset and drop threat" : "rescue: attacked, drop threat";
        }
        // Off the global cooldown (Taunt, Growl, Vanish): no action lost.
        float const cost = gcd ? Value::LostAction(world, *self, Value::GCD_SEC) : 0.0f;
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }

    bool RescueBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const
    {
        RescueDuty const* duty = plan.FindRescueFor(bot->me->GetObjectGuid());
        // The pet's own (Shell Shield, Cower; its Threat way is ThreatBehavior's).
        if (!duty)
            if (Pet const* pet = bot->me->GetPet())
                duty = plan.FindRescueFor(pet->GetObjectGuid());
        // Also for Threat: TargetBehavior switches the tank's target on that tick.
        return world.HasPartyEnemy() && duty;
    }
}
