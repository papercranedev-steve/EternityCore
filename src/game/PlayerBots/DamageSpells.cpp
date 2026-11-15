#include "DamageSpells.h"
#include "HealSpells.h"
#include "Unit.h"
#include "Spell.h"
#include "SpellEntry.h"
#include <algorithm>

namespace DamageSpells
{
    namespace
    {
        // Average rolled base value of one effect (base + a die between baseDice and dieSides).
        float AverageBase(SpellEntry const* spell, uint32 i)
        {
            float const dice = float(spell->EffectBaseDice[i]);
            float const sides = float(spell->EffectDieSides[i]);
            float const roll = sides > dice ? (dice + sides) / 2.0f : dice;
            return float(spell->EffectBasePoints[i]) + roll;
        }

        bool IsPeriodicDamage(SpellEntry const* spell, uint32 i)
        {
            return (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA || spell->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                   spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE && spell->EffectAmplitude[i] > 0;
        }
    }

    float Estimate(Unit* caster, Unit const* target, SpellEntry const* spell)
    {
        float total = 0.0f;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectIndex const idx = SpellEffectIndex(i);
            if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                float const value = caster->SpellDamageBonusDone(target, spell, idx, AverageBase(spell, i), SPELL_DIRECT_DAMAGE);
                total += std::max(0.0f, value);
            }
            else if (IsPeriodicDamage(spell, i))
            {
                int32 const ticks = spell->GetDuration() / int32(spell->EffectAmplitude[i]);
                if (ticks <= 0)
                    continue;
                float const tick = caster->SpellDamageBonusDone(target, spell, idx, AverageBase(spell, i), DOT);
                total += std::max(0.0f, tick) * float(ticks);
            }
        }
        return total;
    }

    float EstimateDirect(Unit* caster, Unit const* target, SpellEntry const* spell)
    {
        float total = 0.0f;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                total += std::max(0.0f, caster->SpellDamageBonusDone(target, spell, SpellEffectIndex(i),
                                                                     AverageBase(spell, i), SPELL_DIRECT_DAMAGE));
        return total;
    }

    Periodic EstimatePeriodic(Unit* caster, Unit const* target, SpellEntry const* spell)
    {
        Periodic p;
        float total = 0.0f;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            bool const leech = spell->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                               spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_LEECH && spell->EffectAmplitude[i] > 0;
            if (!IsPeriodicDamage(spell, i) && !leech)
                continue;
            uint32 const period = spell->EffectAmplitude[i];
            int32 const ticks = spell->GetDuration() / int32(period);
            if (ticks <= 0)
                continue;
            float const tick = caster->SpellDamageBonusDone(target, spell, SpellEffectIndex(i), AverageBase(spell, i), DOT);
            total += std::max(0.0f, tick) * float(ticks);
            if (!p.periodMs || period < p.periodMs)
            {
                p.periodMs = period;
                p.ticks = uint32(ticks);
            }
        }
        if (p.ticks)
            p.perTick = total / float(p.ticks);
        return p;
    }

    float BusySec(Unit const* caster, SpellEntry const* spell)
    {
        float sec = spell->IsChanneledSpell() ? std::max(0, spell->GetDuration()) / 1000.0f
                                              : spell->GetCastTime(caster) / 1000.0f;
        return std::max(GCD_SEC, sec);
    }

    float ManaCost(Unit* caster, SpellEntry const* spell)
    {
        return HealSpells::ManaCost(caster, spell);
    }
}
