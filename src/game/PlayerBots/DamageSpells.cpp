#include "DamageSpells.h"
#include "HealSpells.h"
#include "Unit.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Player.h"
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

        // First rank of Bloodthirst (spell_warrior.cpp WarriorBloodthirstScript: its school
        // damage is base points % of the attack power).
        uint32 const SPELL_BLOODTHIRST_1 = 23881;

        bool IsMeleeClass(SpellEntry const* spell)
        {
            return spell->DmgClass == SPELL_DAMAGE_CLASS_MELEE || spell->DmgClass == SPELL_DAMAGE_CLASS_RANGED;
        }

        WeaponAttackType AttackTypeOf(SpellEntry const* spell)
        {
            return spell->DmgClass == SPELL_DAMAGE_CLASS_RANGED ? RANGED_ATTACK : BASE_ATTACK;
        }

        // Average hit of one weapon damage line of the caster's `type` weapon
        // (Unit::CalculateDamage without the roll).
        float AverageWeaponHit(Unit const* caster, WeaponAttackType type, bool normalized, uint8 index)
        {
            if (Player const* player = caster->ToPlayer())
            {
                float min = 0.0f, max = 0.0f;
                player->CalculateMinMaxDamage(type, normalized, min, max, index);
                return (min + max) / 2.0f;
            }
            if (index)
                return 0.0f;
            uint16 minField = UNIT_FIELD_MINDAMAGE, maxField = UNIT_FIELD_MAXDAMAGE;
            if (type == RANGED_ATTACK)
            {
                minField = UNIT_FIELD_MINRANGEDDAMAGE;
                maxField = UNIT_FIELD_MAXRANGEDDAMAGE;
            }
            return (caster->GetFloatValue(minField) + caster->GetFloatValue(maxField)) / 2.0f;
        }

        // The weapon damage effects together, as Spell::EffectWeaponDmg adds them (one hit);
        // 0 = the spell has none. Before the melee bonus.
        float WeaponDamage(Unit const* caster, SpellEntry const* spell)
        {
            bool any = false;
            bool normalized = false;
            float bonus = 0.0f;
            float percent = 1.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                switch (spell->Effect[i])
                {
                    case SPELL_EFFECT_WEAPON_DAMAGE:
                    case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                        bonus += AverageBase(spell, i);
                        any = true;
                        break;
                    case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                        bonus += AverageBase(spell, i);
                        normalized = true;
                        any = true;
                        break;
                    case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                        percent *= AverageBase(spell, i) / 100.0f;
                        any = true;
                        break;
                    default:
                        break;
                }
            }
            if (!any)
                return 0.0f;
            WeaponAttackType const type = AttackTypeOf(spell);
            UnitMods const mod = type == RANGED_ATTACK ? UNIT_MOD_DAMAGE_RANGED : UNIT_MOD_DAMAGE_MAINHAND;
            float total = bonus * percent * caster->GetModifierValue(mod, TOTAL_PCT);
            for (uint8 i = 0; i < caster->GetWeaponDamageCount(type); ++i)
                total += AverageWeaponHit(caster, type, normalized, i) * percent;
            return std::max(0.0f, total);
        }

        // Base of a school damage effect: Bloodthirst's is a share of the attack power (its script).
        float SchoolBase(Unit const* caster, SpellEntry const* spell, uint32 i)
        {
            float const base = AverageBase(spell, i);
            if (i == EFFECT_INDEX_0 && sSpellMgr.GetFirstSpellInChain(spell->Id) == SPELL_BLOODTHIRST_1)
                return base * caster->GetTotalAttackPowerValue(BASE_ATTACK) / 100.0f;
            return base;
        }

        // One direct hit of school damage effect `i` with the caster's bonus: the melee bonus for
        // a melee spell (SpellCaster::CalculateSpellDamage), the spell power bonus otherwise.
        float SchoolHit(Unit* caster, Unit const* target, SpellEntry const* spell, uint32 i)
        {
            SpellEffectIndex const idx = SpellEffectIndex(i);
            float const base = SchoolBase(caster, spell, i);
            if (IsMeleeClass(spell))
                return std::max(0.0f, caster->MeleeDamageBonusDone(target, base, AttackTypeOf(spell), spell, idx, SPELL_DIRECT_DAMAGE));
            return std::max(0.0f, caster->SpellDamageBonusDone(target, spell, idx, base, SPELL_DIRECT_DAMAGE));
        }

        // The weapon damage effects with the melee bonus (on the last weapon effect, as the server).
        float WeaponHit(Unit* caster, Unit const* target, SpellEntry const* spell)
        {
            float const weapon = WeaponDamage(caster, spell);
            if (weapon <= 0.0f)
                return 0.0f;
            return std::max(0.0f, caster->MeleeDamageBonusDone(target, weapon, AttackTypeOf(spell), spell, EFFECT_INDEX_0,
                                                               SPELL_DIRECT_DAMAGE));
        }
    }

    float Estimate(Unit* caster, Unit const* target, SpellEntry const* spell)
    {
        float total = WeaponHit(caster, target, spell);
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectIndex const idx = SpellEffectIndex(i);
            if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                total += SchoolHit(caster, target, spell, i);
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
        float total = WeaponHit(caster, target, spell);
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                total += SchoolHit(caster, target, spell, i);
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

    float PowerCost(Unit* caster, SpellEntry const* spell)
    {
        Powers const power = Powers(spell->powerType);
        if (power == POWER_MANA || power == POWER_HEALTH)
            return 0.0f;
        return float(Spell::CalculatePowerCost(spell, caster));
    }
}
