#include "HealSpells.h"
#include "Unit.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace HealSpells
{
    namespace
    {
        constexpr float NO_LIMIT = 1.0e9f;

        // Average rolled base value of one effect (base + a die between baseDice and dieSides).
        float AverageBase(SpellEntry const* spell, uint32 i)
        {
            float const dice = float(spell->EffectBaseDice[i]);
            float const sides = float(spell->EffectDieSides[i]);
            float const roll = sides > dice ? (dice + sides) / 2.0f : dice;
            return float(spell->EffectBasePoints[i]) + roll;
        }

        // `a` is better than `b` by more than TIE_FRACTION.
        bool ClearlyMore(float a, float b)
        {
            return a > b * (1.0f + TIE_FRACTION);
        }
    }

    SpellEntry const* HealingPart(SpellEntry const* spell)
    {
        if (!spell)
            return spell;
        uint32 healId = 0;
        switch (spell->Id)
        {
            case 20473: healId = 25914; break;     // Holy Shock rank 1
            case 20929: healId = 25913; break;     // rank 2
            case 20930: healId = 25903; break;     // rank 3
            default: return spell;
        }
        SpellEntry const* heal = sSpellMgr.GetSpellEntry(healId);
        return heal ? heal : spell;
    }

    Amount Estimate(Unit* healer, Unit const* target, SpellEntry const* spell)
    {
        spell = HealingPart(spell);
        Amount out;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectIndex const idx = SpellEffectIndex(i);
            if (spell->Effect[i] == SPELL_EFFECT_HEAL)
            {
                float value = AverageBase(spell, i);
                value = healer->SpellHealingBonusDone(target, spell, idx, value, SPELL_DIRECT_DAMAGE);
                value = target->SpellHealingBonusTaken(healer, spell, idx, value, SPELL_DIRECT_DAMAGE);
                out.direct += std::max(0.0f, value);
            }
            else if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL && spell->EffectAmplitude[i] > 0)
            {
                int32 const duration = spell->GetDuration();
                int32 const ticks = duration / int32(spell->EffectAmplitude[i]);
                if (ticks <= 0)
                    continue;
                float tick = AverageBase(spell, i);
                tick = healer->SpellHealingBonusDone(target, spell, idx, tick, DOT);
                tick = target->SpellHealingBonusTaken(healer, spell, idx, tick, DOT);
                out.periodic += std::max(0.0f, tick) * float(ticks);
                out.periodicSec = std::max(out.periodicSec, float(duration) / 1000.0f);
            }
        }
        return out;
    }

    float EstimateDirectWithBase(Unit* healer, Unit const* target, SpellEntry const* spell, float extraBase)
    {
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spell->Effect[i] != SPELL_EFFECT_HEAL)
                continue;
            SpellEffectIndex const idx = SpellEffectIndex(i);
            float value = AverageBase(spell, i) + extraBase;
            value = healer->SpellHealingBonusDone(target, spell, idx, value, SPELL_DIRECT_DAMAGE);
            value = target->SpellHealingBonusTaken(healer, spell, idx, value, SPELL_DIRECT_DAMAGE);
            return std::max(0.0f, value);
        }
        return 0.0f;
    }

    float ManaCost(Unit* caster, SpellEntry const* spell)
    {
        if (spell->powerType != POWER_MANA)
            return 0.0f;
        return float(Spell::CalculatePowerCost(spell, caster));
    }

    bool IsSingleTargetHeal(SpellEntry const* spell)
    {
        spell = HealingPart(spell);
        if (spell->IsAreaOfEffectSpell())
            return false;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            // A party aura that heals (Tranquility): a group heal.
            if (spell->Effect[i] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY && spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL)
                return false;
            if (spell->Effect[i] != SPELL_EFFECT_HEAL &&
                !(spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL))
                continue;
            for (uint32 target : { spell->EffectImplicitTargetA[i], spell->EffectImplicitTargetB[i] })
                if (Spells::IsAreaEffectTarget(SpellTarget(target)) ||
                    Spells::IsAreaEffectPossitiveTarget(SpellTarget(target)))
                    return false;
        }
        return true;
    }

    float TimeToDie(Situation const& s)
    {
        if (s.incomingDps <= 0.0f)
            return NO_LIMIT;
        return s.health / s.incomingDps;
    }

    bool IsEmergency(Situation const& s)
    {
        if (s.incomingDps <= 0.0f || s.maxHealth <= 0.0f)
            return false;
        return s.health * 100.0f / s.maxHealth < EMERGENCY_PCT || TimeToDie(s) < EMERGENCY_TTD_SEC;
    }

    int Choose(Situation const& s, std::vector<Option>& options)
    {
        float const incoming = std::max(0.0f, s.incomingDps);
        float const missing = std::max(0.0f, s.maxHealth - s.health) + std::max(0.0f, s.extraNeed);

        for (Option& o : options)
        {
            float const busy = std::max(GCD_SEC, o.castSec);
            float const atLanding = std::min(s.maxHealth, missing + incoming * o.castSec);
            o.effectiveDirect = std::min(o.amount.direct, atLanding);
            float effectivePeriodic = 0.0f;
            if (o.amount.periodic > 0.0f)
                effectivePeriodic = std::min(o.amount.periodic, std::max(0.0f, atLanding - o.effectiveDirect) +
                                             incoming * o.amount.periodicSec * HOT_INCOMING_SHARE);
            o.effective = o.effectiveDirect + effectivePeriodic;
            o.perMana = o.mana > 0.0f ? o.effective / o.mana : o.effective * 1000.0f;   // free: always best
            o.throughput = o.effectiveDirect / busy;
            if (effectivePeriodic > 0.0f)
                o.throughput += effectivePeriodic / std::max(busy, o.amount.periodicSec);
        }

        int best = -1;
        if (IsEmergency(s))
        {
            float const ttd = TimeToDie(s);
            // The biggest direct heal that lands in time.
            for (size_t i = 0; i < options.size(); ++i)
            {
                Option const& o = options[i];
                if (o.effectiveDirect <= 0.0f || o.castSec + LANDING_MARGIN_SEC >= ttd)
                    continue;
                if (best < 0)
                {
                    best = int(i);
                    continue;
                }
                Option const& b = options[best];
                if (ClearlyMore(o.effectiveDirect, b.effectiveDirect) ||
                    (!ClearlyMore(b.effectiveDirect, o.effectiveDirect) && o.perMana > b.perMana))
                    best = int(i);
            }
            if (best >= 0)
                return best;
            // Nothing lands in time: the fastest direct heal.
            for (size_t i = 0; i < options.size(); ++i)
            {
                Option const& o = options[i];
                if (o.effectiveDirect <= 0.0f)
                    continue;
                if (best < 0 || o.castSec < options[best].castSec ||
                    (o.castSec == options[best].castSec && o.effectiveDirect > options[best].effectiveDirect))
                    best = int(i);
            }
            if (best >= 0)
                return best;
            // No direct heal at all: fall through to the normal choice (HoTs).
        }

        float const buffer = s.health - s.maxHealth * SAFE_FLOOR_PCT / 100.0f;
        float const required = incoming - buffer / HORIZON_SEC;
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (o.effective <= 0.0f || o.throughput < required)
                continue;
            if (best < 0)
            {
                best = int(i);
                continue;
            }
            Option const& b = options[best];
            if (ClearlyMore(o.perMana, b.perMana) ||
                (!ClearlyMore(b.perMana, o.perMana) && o.effective > b.effective))
                best = int(i);
        }
        if (best >= 0)
            return best;

        // Nothing keeps up: the highest throughput.
        for (size_t i = 0; i < options.size(); ++i)
            if (options[i].effective > 0.0f && (best < 0 || options[i].throughput > options[best].throughput))
                best = int(i);
        return best;
    }

    bool IsInDanger(Situation const& s, float fastestHealSec)
    {
        if (s.incomingDps <= 0.0f || s.maxHealth <= 0.0f)
            return false;
        if (s.health * 100.0f / s.maxHealth < SHIELD_LOW_PCT)
            return true;
        return TimeToDie(s) < fastestHealSec + SHIELD_MARGIN_SEC;
    }
}
