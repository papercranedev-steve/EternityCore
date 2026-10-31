#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Perception/ManaBudget.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Spell.h"
#include "SpellEntry.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Commander
{
    namespace
    {
        // Mana the spell costs this caster now (talents and auras included, no spell mods
        // that need a Spell object). 0 for spells that do not use mana.
        float ManaCost(Player* caster, SpellEntry const* spell)
        {
            if (spell->powerType != POWER_MANA)
                return 0.0f;
            return float(Spell::CalculatePowerCost(spell, caster));
        }

        // Healing the spell does on `target`: direct heal effects with the caster's and
        // target's healing bonuses (like ThreatBehavior), plus the base total of periodic
        // heal auras (Regrowth, Renew; no bonus, ticks = duration / amplitude).
        float EstimateHeal(Player* healer, Unit* target, SpellEntry const* spell)
        {
            float total = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                SpellEffectIndex const idx = SpellEffectIndex(i);
                if (spell->Effect[i] == SPELL_EFFECT_HEAL)
                {
                    float value = float(spell->CalculateSimpleValue(idx));
                    value = healer->SpellHealingBonusDone(target, spell, idx, value, SPELL_DIRECT_DAMAGE);
                    value = target->SpellHealingBonusTaken(healer, spell, idx, value, SPELL_DIRECT_DAMAGE);
                    total += value;
                }
                else if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL && spell->EffectAmplitude[i] > 0)
                {
                    float const ticks = float(spell->GetDuration() / int32(spell->EffectAmplitude[i]));
                    total += ticks * float(spell->CalculateSimpleValue(idx));
                }
            }
            return std::max(0.0f, total);
        }

        // The measure CombatBotBaseAI::SelectMostEfficientHealingSpell compares with the
        // missing health (base points, periodic auras counted over their duration).
        int32 SelectionPoints(SpellEntry const* spell)
        {
            int32 points = 0;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                switch (spell->Effect[i])
                {
                    case SPELL_EFFECT_HEAL:
                        points += spell->EffectBasePoints[i];
                        break;
                    case SPELL_EFFECT_APPLY_AURA:
                    case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                    case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                        if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL && spell->EffectAmplitude[i] > 0)
                            points += (spell->GetDuration() / int32(spell->EffectAmplitude[i])) * spell->EffectBasePoints[i];
                        break;
                    default:
                        break;
                }
            }
            return points;
        }

        // What the rotation would cast on the target (CombatBotBaseAI::HealInjuredTarget).
        struct HealPrediction
        {
            SpellEntry const* spell = nullptr;
            bool periodic = false;
            std::vector<SpellEntry const*> castable;    // direct heals, strongest first
            size_t index = 0;                           // of `spell` in `castable`
        };

        // Mirror of SelectMostEfficientHealingSpell: the list is strongest first; the closest
        // value to the missing health wins, and the scan stops at the first one below it.
        template <class List>
        SpellEntry const* MirrorSelect(PartyBotAI const* bot, Unit const* target, int32 missing, List const& list,
                                       std::vector<SpellEntry const*>* castable, size_t* index)
        {
            SpellEntry const* best = nullptr;
            int32 bestDiff = std::numeric_limits<int32>::max();
            bool stopped = false;
            for (SpellEntry const* spell : list)
            {
                if (!bot->CanTryToCastSpell(target, spell))
                    continue;
                if (castable)
                    castable->push_back(spell);
                if (stopped)
                {
                    if (!castable)
                        break;
                    continue;
                }
                int32 const diff = SelectionPoints(spell) - missing;
                if (std::abs(diff) < bestDiff)
                {
                    bestDiff = diff;
                    best = spell;
                    if (index && castable)
                        *index = castable->size() - 1;
                }
                if (diff < 0)
                    stopped = true;
            }
            return best;
        }

        HealPrediction PredictHeal(PartyBotAI const* bot, Unit* target)
        {
            HealPrediction p;
            int32 const missing = int32(target->GetMaxHealth() - target->GetHealth());
            // A HoT first when only missing a little health and none is on it yet.
            if (target->GetHealthPercent() >= 80.0f && !target->HasAuraType(SPELL_AURA_PERIODIC_HEAL) &&
                !bot->m_spellListPeriodicHeal.empty())
            {
                if (SpellEntry const* hot = MirrorSelect(bot, target, missing, bot->m_spellListPeriodicHeal, nullptr, nullptr))
                {
                    p.spell = hot;
                    p.periodic = true;
                    return p;
                }
            }
            p.spell = MirrorSelect(bot, target, missing, bot->m_spellListDirectHeal, &p.castable, &p.index);
            return p;
        }

        // E: average healing per mana over the bot's direct heals, measured on itself.
        // Every rank counts the same; which one the rotation casts depends on the missing health.
        float AverageHealPerMana(PartyBotAI const* bot)
        {
            Player* me = bot->me;
            float sum = 0.0f;
            uint32 count = 0;
            for (SpellEntry const* spell : bot->m_spellListDirectHeal)
            {
                float const cost = ManaCost(me, spell);
                float const heal = EstimateHeal(me, me, spell);
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
            if (m.isBot && !m.isPet)
                return m.role == ROLE_TANK;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.victim == m.guid)
                    return true;
            return false;
        }

        bool BelowCeiling(WorldModel const& world, PartyMemberFacts const& m, float tension)
        {
            return m.alive && !m.charmed && m.maxHealth > 0 &&
                   m.healthPct < ManaBudget::HealCeilingPct(tension, IsTankLike(world, m));
        }

        // The heal makes an enemy turn on the healer (same test as ThreatBehavior::ProposeHealer).
        bool PullsAggro(PartyBotAI const* bot, WorldModel const& world, Unit* target, SpellEntry const* spell, float effective)
        {
            ObjectGuid const self = bot->me->GetObjectGuid();
            std::vector<ThreatView::EnemyThreatDelta> deltas;
            ThreatView::PredictHealThreat(bot->me, target, effective, spell, deltas);
            for (ThreatView::EnemyThreatDelta const& d : deltas)
            {
                if (d.added <= 0.0f)
                    continue;
                EnemyFacts const* e = world.FindEnemy(d.enemy);
                if (!e || ThreatView::Headroom(*e, self) - d.added < 0.0f)
                    return true;    // unknown enemy: an add with 0 threat, any heal pulls it
            }
            return false;
        }
    }

    bool ManaBudgetBehavior::ComputeBudget(PartyBotAI const* bot, WorldModel const& world, Budget& out)
    {
        Player* me = bot->me;
        if (me->GetMaxPower(POWER_MANA) == 0)
            return false;
        // Before the party's damage is measured, T is a level-based guess: do not throttle on it.
        if (!world.rates.dealtMeasured)
            return false;

        // T: current enemies only. Waves that have not arrived yet (combat scenario 9) are
        // not counted, so the budget is too generous at the start of a wave.
        out.fightSec = Value::FightRemainingSec(world);
        out.mana = float(me->GetPower(POWER_MANA));

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

    bool ManaBudgetBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER;
    }

    void ManaBudgetBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        if (world.enemies.empty())
            return;

        Budget budget;
        if (!ComputeBudget(bot, world, budget) || budget.tension <= 1.0f)
            return;

        Player* me = bot->me;
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

        HealPrediction const predicted = PredictHeal(bot, pTarget);
        if (!predicted.spell)
            return;

        float const missing = float(pTarget->GetMaxHealth() - pTarget->GetHealth());
        auto savedPoints = [&](float mana)
        {
            return Value::Points(world, ManaBudget::SavedManaHealth(mana, budget.healPerMana, budget.tension));
        };

        // 1. Above the ceiling: skip this heal. Not when someone else is below their ceiling:
        // the veto stops FindAndHealInjuredAlly this tick and they would not be healed either.
        if (healthPct >= ManaBudget::HealCeilingPct(budget.tension, IsTankLike(world, *targetFacts)))
        {
            bool othersNeedIt = false;
            for (PartyMemberFacts const& m : world.party)
                if (m.guid != targetFacts->guid && BelowCeiling(world, m, budget.tension))
                    othersNeedIt = true;

            if (!othersNeedIt)
            {
                Proposal veto;
                veto.kind = ProposalKind::Veto;
                veto.source = "mana";
                veto.reason = "mana budget, above heal ceiling";
                veto.veto.type = VetoType::Heal;
                veto.veto.target = pTarget->GetObjectGuid();
                // Cost as in ThreatBehavior::ProposeHealer (death excluded by the check above).
                veto.SetValue(savedPoints(ManaCost(me, predicted.spell)),
                              Value::Points(world, std::min(missing, incoming * delaySec)));
                out.push_back(veto);
                return;
            }
        }

        // 2. A big direct heal: a smaller one with better healing per mana that covers most of it.
        if (predicted.periodic)
            return;
        float const bigCost = ManaCost(me, predicted.spell);
        float const bigHeal = std::min(missing, EstimateHeal(me, pTarget, predicted.spell));
        if (bigCost <= 0.0f || bigHeal <= 0.0f)
            return;

        for (size_t i = predicted.index + 1; i < predicted.castable.size(); ++i)
        {
            // (not named `small`: a macro in the Windows headers)
            SpellEntry const* smaller = predicted.castable[i];
            float const smallCost = ManaCost(me, smaller);
            if (smallCost <= 0.0f || smallCost >= bigCost)
                continue;
            float const smallHeal = std::min(missing, EstimateHeal(me, pTarget, smaller));
            if (smallHeal < SWAP_MIN_COVER * bigHeal || smallHeal / smallCost <= bigHeal / bigCost)
                continue;
            // Cast directly by the arbiter, past ThreatBehavior's heal veto: check aggro here.
            if (healthPct >= ThreatBehavior::HEAL_IGNORE_THREAT_PCT && PullsAggro(bot, world, pTarget, smaller, smallHeal))
                continue;

            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "mana";
            p.reason = "mana budget, smaller heal";
            p.action.type = BotActionType::Cast;
            p.action.target = pTarget->GetObjectGuid();
            p.action.spell = smaller;
            p.SetValue(savedPoints(bigCost - smallCost), Value::Points(world, bigHeal - smallHeal));
            out.push_back(p);
            break;
        }
    }
}
