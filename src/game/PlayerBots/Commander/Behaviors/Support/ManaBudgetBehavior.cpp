#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "Commander/Behaviors/Support/HealPlanBehavior.h"
#include "Commander/Perception/Forecast/ManaBudget.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Commands/BotSpeech.h"
#include "TeamBotAI.h"
#include "HealSpells.h"
#include "DamageSpells.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/ItemKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "SpellMgr.h"
#include "SpellAuraDefines.h"
#include "Chat.h"
#include "Timer.h"
#include "SpellEntry.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace Commander
{
    namespace
    {
        // E: average healing per mana over the bot's direct heals, measured on itself.
        // Every rank counts the same (the rank cast depends on the target, HealSpells.h).
        float AverageHealPerMana(TeamBotAI const* bot)
        {
            Player* me = bot->me;
            float sum = 0.0f;
            uint32 count = 0;
            for (SpellEntry const* spell : bot->m_spellListDirectHeal)
            {
                float const cost = HealSpells::ManaCost(me, spell);
                float const heal = HealSpells::Estimate(me, me, spell).direct;
                if (cost <= 0.0f || heal <= 0.0f)
                    continue;
                sum += heal / cost;
                ++count;
            }
            return count ? sum / float(count) : ManaBudget::FALLBACK_HEAL_PER_MANA;
        }

        // Bots with the tank role, and players or pets that enemies are hitting right now
        // (the player may be tanking): they keep the higher heal ceiling.
        bool IsTankLike(WorldModel const& world, PartyMemberFacts const& m)
        {
            if (Tanks::IsTank(m))
                return true;
            if (m.isBot && !m.isPet)
                return false;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.victim == m.guid)
                    return true;
            return false;
        }

        bool IsHit(WorldModel const& world, PartyMemberFacts const& self)
        {
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.victim == self.guid)
                    return true;
            return false;
        }

        bool BelowCeiling(WorldModel const& world, PartyMemberFacts const& m, float tension)
        {
            return m.alive && !m.charmed && m.maxHealth > 0 &&
                   m.healthPct < ManaBudget::HealCeilingPct(tension, IsTankLike(world, m));
        }
    }

    bool ManaBudgetBehavior::ComputeBudget(TeamBotAI const* bot, WorldModel const& world, Budget& out)
    {
        // Own mana from the world facts (the mana pool, also while shapeshifted), the live
        // Player only when the bot is not in world.party.
        uint32 mana = 0;
        uint32 maxMana = 0;
        if (PartyMemberFacts const* self = world.FindMember(bot->me->GetObjectGuid()))
        {
            if (!self->UsesMana())
                return false;
            mana = self->mana;
            maxMana = self->maxMana;
        }
        else
        {
            mana = bot->me->GetPower(POWER_MANA);
            maxMana = bot->me->GetMaxPower(POWER_MANA);
        }
        if (maxMana == 0)
            return false;
        // Before the party's damage is measured, T is a level-based guess: do not throttle on it.
        if (!world.rates.dealtMeasured)
            return false;

        // T: current enemies only. Waves that have not arrived yet (combat scenario 9) are
        // not counted, so the budget is too generous at the start of a wave.
        out.fightSec = Value::FightRemainingSec(world);
        out.mana = float(mana);
        out.manaPct = float(mana) * 100.0f / float(maxMana);

        // Members being hit can take damage down to HEADROOM_FLOOR_PCT without a heal.
        // Only those being hit: we do not know where future damage lands, so health of
        // members nobody is hitting is not counted against it.
        out.headroom = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive || Value::IncomingOn(world, m.guid) <= 0.0f)
                continue;
            out.headroom += std::max(0.0f, float(m.health) - float(m.maxHealth) * ManaBudget::HEADROOM_FLOOR_PCT);
        }

        out.neededHealing = ManaBudget::NeededHealing(world.rates.incomingDps, out.fightSec, out.headroom,
                                                      world.rates.healers);
        out.healPerMana = AverageHealPerMana(bot);
        out.neededMana = ManaBudget::NeededMana(out.neededHealing, out.healPerMana);
        out.tension = ManaBudget::Tension(out.neededMana, out.mana);
        return true;
    }

    void ManaBudgetBehavior::NoteLowMana(TeamBotAI const* bot, Budget const& budget) const
    {
        if (!ManaBudget::WantsLowManaWarning(budget.manaPct, budget.tension))
            return;
        Player* leader = bot->GetPartyLeader();
        if (!leader || leader == bot->me || !leader->IsInWorld() || leader->GetMap() != bot->me->GetMap())
            return;

        uint32 const nowMs = WorldTimer::getMSTime();
        uint32 const repeatMs = LOW_MANA_NOTE_REPEAT_MS;
        {
            // A few entries (healer bots), never pruned.
            static std::mutex s_noteLock;
            static std::unordered_map<uint64, uint32> s_lastNoteMs;
            std::lock_guard<std::mutex> lock(s_noteLock);
            uint64 const key = bot->me->GetObjectGuid().GetRawValue();
            auto const itr = s_lastNoteMs.find(key);
            if (itr != s_lastNoteMs.end() && WorldTimer::getMSTimeDiff(itr->second, nowMs) < repeatMs)
                return;
            s_lastNoteMs[key] = nowMs;
        }

        // tension = fight time / time the mana lasts.
        char text[200];
        snprintf(text, sizeof(text), "%s is low on mana (%.0f%%): about %.0f s of fight left, mana for about %.0f s of it.",
                 bot->me->GetName(), budget.manaPct, budget.fightSec, budget.fightSec / budget.tension);
        BotSpeech::Report(bot->me, text);
    }

    bool ManaBudgetBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER || bot->Kit().SelfManaRestore() ||
               (bot->me->GetMaxPower(POWER_MANA) > 0 && ItemKit::HoldsAny(*bot, ItemKit::Use::Mana));
    }

    void ManaBudgetBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (world.enemies.empty())
            return;

        ProposeSelfRestore(bot, world, out);
        ProposeManaItem(bot, world, plan, out);
        if (bot->GetRole() == ROLE_HEALER)
            ProposeHealVeto(bot, world, out);
    }

    void ManaBudgetBehavior::ProposeSelfRestore(TeamBotAI const* bot, WorldModel const& world,
                                                std::vector<Proposal>& out) const
    {
        SpellEntry const* restore = bot->Kit().SelfManaRestore();
        SpellEntry const* filler = bot->Kit().Filler();
        Player* me = bot->me;
        if (!restore || !filler || !me->IsInCombat() || !world.rates.dealtMeasured)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->UsesMana() || !self->CanAct())
            return;
        // Hits push the channel back and cut it short.
        if (IsHit(world, *self))
            return;
        if (!bot->CanTryToCastSpell(me, restore))
            return;

        float const spend = DamageSpells::ManaCost(me, filler) / DamageSpells::BusySec(me, filler) - self->manaRegenCasting;
        if (spend <= 0.0f)
            return;
        float const fightSec = Value::FightRemainingSec(world);
        float const mana = float(self->mana);
        float const channelSec = DamageSpells::BusySec(me, restore);
        float regenPct = 0.0f;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (restore->Effect[i] == SPELL_EFFECT_APPLY_AURA && restore->EffectApplyAuraName[i] == SPELL_AURA_MOD_POWER_REGEN_PERCENT)
                regenPct += float(restore->CalculateSimpleValue(SpellEffectIndex(i)));
        float const fullRestore = self->manaRegen * (1.0f + regenPct / 100.0f) * channelSec;
        float const missing = float(self->maxMana) - mana;
        // Restoring now is capped by the missing mana: a few more casts first make the same
        // channel give more. So wait until it all fits, unless the next fillers cannot be paid
        // (strat-20261102-220932: Evocation at 79% mana restored 1100 of its ~4000).
        if (missing < fullRestore && mana >= DamageSpells::ManaCost(me, filler) * DRY_FILLER_CASTS)
            return;
        float const restored = std::min(missing, fullRestore);

        float const castWithout = std::min(fightSec, mana / spend);
        float const castWith = std::min(std::max(0.0f, fightSec - channelSec), (mana + restored) / spend);
        if (castWith <= castWithout)
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "mana";
        p.reason = "mana runs out before the fight ends: restore";
        p.action.type = BotActionType::Cast;
        p.action.spell = restore;
        p.SetValue(Value::LostAction(world, *self, castWith), Value::LostAction(world, *self, castWithout));
        out.push_back(p);
    }

    void ManaBudgetBehavior::ProposeManaItem(TeamBotAI const* bot, WorldModel const& world,
                                             TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!me->IsInCombat() || !world.rates.dealtMeasured)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->UsesMana() || !self->CanAct() || self->manaItems.empty())
            return;

        // Own spells before consumables (user 2026-11-15): Innervate / Mana Tide on its way, or
        // its own restore ready while nothing hits it (ProposeSelfRestore casts that).
        for (BoostDuty const& d : plan.boosts)
            if (d.target == self->guid)
                return;
        if (SpellEntry const* restore = bot->Kit().SelfManaRestore())
            if (me->IsSpellReady(restore) && !IsHit(world, *self))
                return;

        bool const healer = bot->GetRole() == ROLE_HEALER;
        // Won without it (user 2026-11-15): a damage dealer running dry only makes the kill
        // slower; only a boss fight is worth a consumable for that. A healer: the budget below
        // is 0 while the mana lasts the fight.
        if (!healer && std::none_of(world.enemies.begin(), world.enemies.end(),
                                    [](EnemyFacts const& e) { return e.boss && e.acting; }))
            return;

        float const mana = float(self->mana);
        float const missing = float(self->maxMana) - mana;
        float const fightSec = Value::FightRemainingSec(world);

        // What `restored` more mana is worth to this bot (header).
        Budget budget;
        bool const hasBudget = healer && ComputeBudget(bot, world, budget);
        float spend = 0.0f;
        if (!healer)
            if (SpellEntry const* filler = bot->Kit().Filler())
                spend = DamageSpells::ManaCost(me, filler) / DamageSpells::BusySec(me, filler) - self->manaRegenCasting;
        auto worth = [&](float restored)
        {
            if (hasBudget)
                return Value::Points(world, ManaBudget::SavedManaHealth(restored, budget.healPerMana, budget.tension));
            if (spend <= 0.0f)
                return 0.0f;
            float const castWithout = std::min(fightSec, mana / spend);
            float const castWith = std::min(fightSec, (mana + restored) / spend);
            return Value::LostAction(world, *self, castWith) - Value::LostAction(world, *self, castWithout);
        };
        bool const inDanger = SurvivalValue::Of(world, *self).InDanger();

        ManaItemAbility const* best = nullptr;
        float bestBenefit = 0.0f;
        float bestCost = 0.0f;
        for (ManaItemAbility const& a : self->manaItems)
        {
            // Not before its whole amount fits (cooldown doc, spell list section 5).
            if (!a.ready || missing < a.mana)
                continue;
            if (a.healthCost > 0.0f &&
                (inDanger || float(self->health) - a.healthCost < float(self->maxHealth) * RUNE_MIN_HEALTH_PCT / 100.0f))
                continue;
            float const benefit = worth(a.mana);
            float const cost = Value::CooldownPoints(a.cooldownMs / 1000.0f, fightSec + SurvivalValue::NEXT_FIGHTS_SEC) +
                               Value::Points(world, a.healthCost);
            if (benefit <= cost || (best && benefit - cost <= bestBenefit - bestCost))
                continue;
            best = &a;
            bestBenefit = benefit;
            bestCost = cost;
        }
        if (!best)
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(best->spellId);
        if (!spell)
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "mana";
        p.reason = best->healthCost > 0.0f ? "mana runs short: rune" : "mana runs short: potion or gem";
        p.action.type = BotActionType::UseItem;
        p.action.itemId = best->itemId;
        p.action.spell = spell;
        p.action.target = self->guid;
        p.SetValue(bestBenefit, bestCost);
        out.push_back(p);
    }

    void ManaBudgetBehavior::ProposeHealVeto(TeamBotAI const* bot, WorldModel const& world,
                                             std::vector<Proposal>& out) const
    {
        Budget budget;
        if (!ComputeBudget(bot, world, budget))
            return;
        NoteLowMana(bot, budget);
        if (budget.tension <= 1.0f)
            return;
        // The heal module weighs mana itself (HealPlan::ManaWeight); nothing to veto then.
        if (HealPlanBehavior::ControlsHealingOf(bot, world))
            return;

        Unit* pTarget = bot->SelectHealTarget();     // same call as FindAndHealInjuredAlly()
        if (!pTarget)
            return;
        PartyMemberFacts const* targetFacts = world.FindMember(pTarget->GetObjectGuid());
        if (!targetFacts)
            return;

        float const healthPct = pTarget->GetHealthPercent();
        if (healthPct < EMERGENCY_PCT)
            return;

        // Waiting must not kill it: damage until the next ticks against its health.
        float const delaySec = DELAY_TICKS * sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        float const incoming = Value::IncomingOn(world, targetFacts->guid);
        if (incoming * delaySec >= float(pTarget->GetHealth()))
            return;

        // The heal the rotation would cast (CombatBotBaseAI::HealInjuredTarget).
        HealChoice choice;
        if (!bot->ChooseHeal(pTarget, choice))
            return;

        float const missing = float(pTarget->GetMaxHealth() - pTarget->GetHealth());
        auto savedPoints = [&](float mana)
        {
            return Value::Points(world, ManaBudget::SavedManaHealth(mana, budget.healPerMana, budget.tension));
        };

        // Above the ceiling: skip this heal. Not when someone else is below their ceiling:
        // the veto stops FindAndHealInjuredAlly this tick and they would not be healed either.
        // (Until 2026-11-01 a second option swapped a big heal for a smaller, more efficient
        // rank. ChooseHeal now picks the most efficient rank that keeps up itself.)
        if (healthPct < ManaBudget::HealCeilingPct(budget.tension, IsTankLike(world, *targetFacts)))
            return;
        for (PartyMemberFacts const& m : world.party)
            if (m.guid != targetFacts->guid && BelowCeiling(world, m, budget.tension))
                return;

        Proposal veto;
        veto.kind = ProposalKind::Veto;
        veto.source = "mana";
        veto.reason = "mana budget, above heal ceiling";
        veto.veto.type = VetoType::Heal;
        veto.veto.target = pTarget->GetObjectGuid();
        // Cost as in ThreatBehavior::ProposeHealer (death excluded by the check above).
        veto.SetValue(savedPoints(choice.Chosen().mana),
                      Value::Points(world, std::min(missing, incoming * delaySec)));
        out.push_back(veto);
    }
}
