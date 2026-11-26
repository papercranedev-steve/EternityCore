#include "Commander/Behaviors/Support/HealthTradeBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "DamageSpells.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Nothing hurts it and nothing is about to: no enemy on it or casting at it, no damage
        // taken within DamageIntake's window (damage over time, area spells, a hit just now).
        bool Safe(WorldModel const& world, PartyMemberFacts const& self)
        {
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.crowdControlled)
                    continue;
                if (e.victim == self.guid || (e.castingSpellId && e.castTarget == self.guid))
                    return false;
            }
            return Value::TakenDps(world, self.guid) <= 0.0f;
        }

        // Filler mana per second while casting it, less the regeneration while casting.
        float Spend(Player* me, PartyMemberFacts const& self, SpellEntry const* filler)
        {
            return DamageSpells::ManaCost(me, filler) / DamageSpells::BusySec(me, filler) - self.manaRegenCasting;
        }

        // Seconds of filler casting `mana` pays for before the fight ends, on top of `have`.
        float PaidSec(float fightSec, float spend, float have, float mana)
        {
            return std::min(fightSec, (have + mana) / spend) - std::min(fightSec, have / spend);
        }

        // Its health can pay a tap now: above the tap's cost (Life Tap fizzles at or below it,
        // spell_warlock.cpp: it cannot kill), safe, not in danger.
        bool HealthPaysTap(WorldModel const& world, PartyMemberFacts const& self)
        {
            if (self.tapManaPerCast <= 0.0f || self.tapManaPerHealth <= 0.0f)
                return false;
            if (float(self.health) <= self.tapManaPerCast / self.tapManaPerHealth)
                return false;
            return Safe(world, self) && !SurvivalValue::Of(world, self).InDanger();
        }

        // The global cooldown would go unused: walking, or the filler cannot reach its victim
        // (none, out of sight, out of range).
        bool FreeGcd(TeamBotAI const* bot, SpellEntry const* filler)
        {
            Player* me = bot->me;
            if (me->IsMoving())
                return true;
            Unit* victim = me->GetVictim();
            if (!victim || !victim->IsAlive() || !me->IsWithinLOSInMap(victim))
                return true;
            return !me->IsWithinDistInMap(victim, CombatBotBaseAI::SpellMaxRange(me, filler));
        }

        void ProposeTap(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                        std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* tap = bot->Kit().HealthToMana();
            SpellEntry const* filler = bot->Kit().Filler();
            if (!tap || !filler || !self.UsesMana() || self.tapManaPerCast <= 0.0f || self.tapManaPerHealth <= 0.0f)
                return;
            float const mana = float(self.mana);
            float const gain = self.tapManaPerCast;
            if (float(self.maxMana) - mana < gain)
                return;
            if (!HealthPaysTap(world, self))
                return;
            if (!bot->CanTryToCastSpell(me, tap))
                return;

            // Just in time (user 2026-11-26): a tap now gives what a tap later gives, so only
            // when the next filler cannot be paid or the global cooldown is free anyway.
            float const fillerCost = DamageSpells::ManaCost(me, filler);
            bool const dry = mana < fillerCost;
            bool const freeGcd = !dry && FreeGcd(bot, filler);
            if (!dry && !freeGcd)
                return;

            // Nothing while the mana it has lasts the fight.
            float const spend = Spend(me, self, filler);
            if (spend <= 0.0f)
                return;
            float const paidSec = PaidSec(Value::FightRemainingSec(world), spend, mana, gain);
            if (paidSec <= 0.0f)
                return;

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "health";
            p.reason = dry ? "no mana for the next cast, nothing hits it: tap"
                           : "the global cooldown is free anyway, nothing hits it: tap";
            p.action.type = BotActionType::Cast;
            p.action.spell = tap;
            p.action.target = self.guid;
            p.SetValue(Value::LostAction(world, self, paidSec), Value::LostAction(world, self, Value::GCD_SEC));
            out.push_back(p);
        }

        void ProposeDrain(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                          std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* drain = bot->Kit().DrainHeal();
            if (!drain || !self.maxHealth || self.health >= self.maxHealth)
                return;
            Unit* victim = me->GetVictim();
            EnemyFacts const* enemy = victim ? world.FindEnemy(victim->GetObjectGuid()) : nullptr;
            if (!enemy || !victim->IsAlive() || !me->IsWithinLOSInMap(victim) || !bot->CanTryToCastSpell(victim, drain))
                return;

            DamageSpells::Periodic const ticks = DamageSpells::EstimatePeriodic(me, victim, drain);
            if (ticks.perTick <= 0.0f || !ticks.periodMs || !ticks.ticks)
                return;
            float const channelSec = DamageSpells::BusySec(me, drain);
            // The ticks it gets in before the victim dies.
            float const dyingSec = Value::EnemyDyingSec(world, *enemy);
            uint32 landed = ticks.ticks;
            if (dyingSec < channelSec)
                landed = std::min(landed, uint32(dyingSec * 1000.0f / float(ticks.periodMs)));
            if (!landed)
                return;
            float const missing = float(self.maxHealth) - float(self.health);
            float const drained = ticks.perTick * float(landed);
            float const gained = std::min(missing, drained);
            float const busySec = std::min(channelSec, float(landed) * float(ticks.periodMs) / 1000.0f);
            float const drainCost = DamageSpells::ManaCost(me, drain);

            SpellEntry const* filler = bot->Kit().Filler();
            float drainShare = 0.0f;    // its damage per second over the filler's
            if (filler)
            {
                float const fillerDps = DamageSpells::Estimate(me, victim, filler) / DamageSpells::BusySec(me, filler);
                if (fillerDps > 0.0f)
                    drainShare = (ticks.perTick * float(ticks.ticks) / channelSec) / fillerDps;
            }

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "health";
            p.action.type = BotActionType::Cast;
            p.action.spell = drain;
            p.action.target = enemy->guid;

            // The cycle (header): its last mana goes into a drain, the health it brings back is
            // tapped into Shadow Bolts again. Not while its health still pays a tap: taps just
            // in time keep the mana low all fight, the drain would take the Shadow Bolts' place.
            HealthTradeBehavior::Cycle const cycle = HealthTradeBehavior::CycleOf(bot, world, self);
            float const mana = float(self.mana);
            if (cycle.on && mana < drainCost + cycle.fillerCost && missing >= drained &&
                !(bot->Kit().HealthToMana() && HealthPaysTap(world, self)))
            {
                float const netSec = (gained * self.tapManaPerHealth - drainCost) / cycle.spend;
                float const benefit = Value::LostAction(world, self, busySec * std::min(1.0f, drainShare)) +
                                      Value::LostAction(world, self, std::max(0.0f, netSec));
                float const cost = Value::LostAction(world, self, busySec) * HealthTradeBehavior::WAND_SHARE;
                p.reason = "mana runs out: drain the health back to tap again";
                p.SetValue(benefit, cost);
                out.push_back(p);
                return;
            }

            // Its own heal under fire.
            if (self.healthPct >= HealthTradeBehavior::DRAIN_BELOW_PCT || Value::TakenDps(world, self.guid) <= 0.0f)
                return;
            float const cost = Value::LostAction(world, self, busySec) * std::max(0.0f, 1.0f - drainShare) +
                               ManaWorth::Points(world, self, drainCost);
            p.reason = "low health, hit: drain";
            p.SetValue(Value::Points(world, gained), cost);
            out.push_back(p);
        }
    }

    HealthTradeBehavior::Cycle HealthTradeBehavior::CycleOf(TeamBotAI const* bot, WorldModel const& world,
                                                            PartyMemberFacts const& self)
    {
        Cycle c;
        Player* me = bot->me;
        SpellEntry const* tap = bot->Kit().HealthToMana();
        SpellEntry const* filler = bot->Kit().Filler();
        if (!tap || !filler || !self.UsesMana() || self.tapManaPerHealth <= 0.0f)
            return c;
        c.fillerCost = DamageSpells::ManaCost(me, filler);
        c.spend = Spend(me, self, filler);
        if (c.spend <= 0.0f)
            return c;
        // Mana and every tap its health still gives: short of the fight, the cycle is on.
        float const bank = float(self.mana) + float(self.health) * self.tapManaPerHealth;
        c.on = bank / c.spend < Value::FightRemainingSec(world);
        return c;
    }

    bool HealthTradeBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->Kit().HealthToMana() || bot->Kit().DrainHeal();
    }

    void HealthTradeBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                      TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!me->IsInCombat() || world.enemies.empty())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct() || self->silenced)
            return;
        ProposeTap(bot, world, *self, out);
        ProposeDrain(bot, world, *self, out);
    }
}
