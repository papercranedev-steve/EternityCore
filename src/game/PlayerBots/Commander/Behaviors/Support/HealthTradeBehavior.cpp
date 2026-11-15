#include "Commander/Behaviors/Support/HealthTradeBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "DamageSpells.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        bool Healed(WorldModel const& world, PartyMemberFacts const& self)
        {
            // Any demand above 0: HealOn shares a healer's rate by the damage taken, and a
            // member taking none would get no share while others are hit.
            return SurvivalValue::HealOn(world, self, SurvivalValue::Demand(world, self) + 1.0f).hps > 0.0f;
        }

        bool IsHit(WorldModel const& world, PartyMemberFacts const& self)
        {
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.victim == self.guid)
                    return true;
            return false;
        }

        void ProposeTap(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                        bool healed, std::vector<Proposal>& out)
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
            float const manaPct = mana * 100.0f / float(self.maxMana);
            if (manaPct >= (healed ? HealthTradeBehavior::TAP_MANA_PCT : HealthTradeBehavior::TAP_MANA_ALONE_PCT))
                return;

            float const fillerCost = DamageSpells::ManaCost(me, filler);
            bool const dry = mana < fillerCost;
            float floorPct = HealthTradeBehavior::TAP_FLOOR_PCT;
            if (!healed)
            {
                if (!dry && IsHit(world, self))
                    return;
                floorPct = dry ? HealthTradeBehavior::TAP_FLOOR_ALONE_PCT : HealthTradeBehavior::DRAIN_BELOW_ALONE_PCT;
            }
            float const healthCost = gain / self.tapManaPerHealth;
            if ((float(self.health) - healthCost) * 100.0f < float(self.maxHealth) * floorPct)
                return;
            if (SurvivalValue::Of(world, self).InDanger())
                return;
            if (!bot->CanTryToCastSpell(me, tap))
                return;

            // Seconds of filler casting the mana pays for.
            float const spend = fillerCost / DamageSpells::BusySec(me, filler) - self.manaRegenCasting;
            if (spend <= 0.0f)
                return;
            float paidSec = gain / spend;
            if (!healed)
            {
                float const fightSec = Value::FightRemainingSec(world);
                paidSec = std::max(0.0f, std::min(fightSec, (mana + gain) / spend) - std::min(fightSec, mana / spend));
            }

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "health";
            p.reason = healed ? "mana low, a healer pays the health back: tap" : "mana runs out, health to spare: tap";
            p.action.type = BotActionType::Cast;
            p.action.spell = tap;
            p.action.target = self.guid;
            p.SetValue(Value::LostAction(world, self, paidSec), Value::LostAction(world, self, Value::GCD_SEC));
            out.push_back(p);
        }

        void ProposeDrain(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                          bool healed, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* drain = bot->Kit().DrainHeal();
            if (!drain || !self.maxHealth)
                return;
            float const below = healed ? HealthTradeBehavior::DRAIN_BELOW_PCT : HealthTradeBehavior::DRAIN_BELOW_ALONE_PCT;
            if (self.healthPct >= below)
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
            float const gained = std::min(missing, ticks.perTick * float(landed));

            // The channel instead of the filler: only the damage it does not do is lost.
            float lostShare = 1.0f;
            if (SpellEntry const* filler = bot->Kit().Filler())
            {
                float const fillerDps = DamageSpells::Estimate(me, victim, filler) / DamageSpells::BusySec(me, filler);
                float const drainDps = ticks.perTick * float(ticks.ticks) / channelSec;
                if (fillerDps > 0.0f)
                    lostShare = std::max(0.0f, 1.0f - drainDps / fillerDps);
            }
            float const busySec = std::min(channelSec, float(landed) * float(ticks.periodMs) / 1000.0f);
            float const cost = Value::LostAction(world, self, busySec) * lostShare +
                               ManaWorth::Points(world, self, DamageSpells::ManaCost(me, drain));

            Proposal p;
            p.tier = ProposalTier::Routine;
            p.source = "health";
            p.reason = healed ? "low health, healers may not keep up: drain" : "low health, no healer: drain";
            p.action.type = BotActionType::Cast;
            p.action.spell = drain;
            p.action.target = enemy->guid;
            p.SetValue(Value::Points(world, gained), cost);
            out.push_back(p);
        }
    }

    bool HealthTradeBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->Kit().HealthToMana() || bot->Kit().DrainHeal();
    }

    void HealthTradeBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                      TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!me->IsInCombat() || world.enemies.empty())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct() || self->silenced)
            return;
        bool const healed = Healed(world, *self);
        ProposeTap(bot, world, *self, healed, out);
        ProposeDrain(bot, world, *self, healed, out);
    }
}
