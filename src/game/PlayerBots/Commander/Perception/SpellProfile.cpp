#include "Commander/Perception/SpellProfile.h"
#include "SpellEntry.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include <algorithm>

namespace Commander
{
namespace SpellProfiles
{
    namespace
    {
        bool IsAuraEffect(uint32 effect)
        {
            return effect == SPELL_EFFECT_APPLY_AURA || effect == SPELL_EFFECT_PERSISTENT_AREA_AURA ||
                   effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY;
        }

        bool IsControlAura(uint32 aura)
        {
            switch (aura)
            {
                case SPELL_AURA_MOD_FEAR:
                case SPELL_AURA_MOD_CONFUSE:
                case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_MOD_POSSESS:
                case SPELL_AURA_MOD_STUN:
                case SPELL_AURA_MOD_SILENCE:
                case SPELL_AURA_MOD_PACIFY_SILENCE:
                    return true;
                default:
                    return false;
            }
        }

        bool ScalesWithCreatureLevel(SpellEntry const* spell, uint32 i)
        {
            // Condition copied from SpellCaster::CalculateSpellEffectValue.
            return spell->HasAttribute(SPELL_ATTR_SCALES_WITH_CREATURE_LEVEL) && spell->spellLevel &&
                   spell->Effect[i] != SPELL_EFFECT_WEAPON_PERCENT_DAMAGE &&
                   spell->Effect[i] != SPELL_EFFECT_KNOCK_BACK &&
                  (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA || spell->EffectApplyAuraName[i] != SPELL_AURA_MOD_DECREASE_SPEED);
        }

        float ClassLevelMelee(uint32 level)
        {
            CreatureClassLevelStats const* cls = sObjectMgr.GetCreatureClassLevelStats(1, std::max(1u, level));
            return cls ? cls->melee_damage : 1.0f;
        }

        // Average of SpellCaster::CalculateSpellEffectValue without spell mods.
        float AverageEffectValue(SpellEntry const* spell, uint32 i, uint32 casterLevel)
        {
            int32 level = int32(casterLevel);
            if (spell->maxLevel > 0 && level > int32(spell->maxLevel))
                level = int32(spell->maxLevel);
            else if (level < int32(spell->baseLevel))
                level = int32(spell->baseLevel);
            level -= int32(spell->spellLevel);

            int32 const baseDice = int32(spell->EffectBaseDice[i]);
            float value = float(spell->EffectBasePoints[i]) + level * spell->EffectRealPointsPerLevel[i];
            int32 const randomPoints = int32(spell->EffectDieSides[i] + level * spell->EffectDicePerLevel[i]);
            if (randomPoints <= 1)
                value += baseDice;
            else
                value += (baseDice + randomPoints) / 2.0f;

            if (ScalesWithCreatureLevel(spell, i))
                value *= ClassLevelMelee(casterLevel) / ClassLevelMelee(spell->spellLevel);
            return value;
        }

        uint32 DurationMs(SpellEntry const* spell)
        {
            return uint32(std::max(0, spell->GetDuration()));
        }
    }

    SpellProfile Build(SpellEntry const* spell)
    {
        SpellProfile p;
        if (!spell)
            return p;

        p.spellId = spell->Id;
        p.category = SpellTags::Categorize(spell);
        p.castMs = spell->GetCastTime(nullptr);
        p.durationMs = DurationMs(spell);
        p.channeled = spell->IsChanneledSpell();
        p.interruptible = SpellTags::IsInterruptible(spell, p.channeled, p.channeled ? 0 : int32(p.castMs));
        p.harmful = !spell->IsPositiveSpell();
        p.dispel = uint8(spell->Dispel);

        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (!spell->Effect[i])
                continue;
            if (SpellRadiusEntry const* radius = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                p.radius = std::max(p.radius, radius->Radius);
            if (IsAuraEffect(spell->Effect[i]) && IsControlAura(spell->EffectApplyAuraName[i]))
                p.controls = true;
        }
        return p;
    }

    SpellAmounts Estimate(SpellEntry const* spell, uint32 casterLevel, float meleeHit)
    {
        SpellAmounts a;
        if (!spell)
            return a;

        float const durationSec = DurationMs(spell) / 1000.0f;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            uint32 const effect = spell->Effect[i];
            if (!effect)
                continue;
            float const value = AverageEffectValue(spell, i, casterLevel);

            switch (effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                    a.damage += std::max(0.0f, value);
                    break;
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                    a.damage += meleeHit + std::max(0.0f, value);
                    break;
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                    a.damage += meleeHit * std::max(0.0f, value) / 100.0f;
                    break;
                case SPELL_EFFECT_HEAL:
                    a.heal += std::max(0.0f, value);
                    break;
                default:
                    break;
            }

            if (!IsAuraEffect(effect))
                continue;

            uint32 const aura = spell->EffectApplyAuraName[i];
            float const ticks = spell->EffectAmplitude[i] ? durationSec * 1000.0f / spell->EffectAmplitude[i] : 0.0f;
            switch (aura)
            {
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_LEECH:
                    a.periodicDamage += std::max(0.0f, value) * ticks;
                    break;
                case SPELL_AURA_PERIODIC_HEAL:
                    a.periodicHeal += std::max(0.0f, value) * ticks;
                    break;
                default:
                    if (IsControlAura(aura))
                        a.controlSec = std::max(a.controlSec, durationSec);
                    break;
            }
        }
        return a;
    }

    char const* DispelName(uint8 dispel)
    {
        switch (dispel)
        {
            case 0: return "none";
            case 1: return "magic";
            case 2: return "curse";
            case 3: return "disease";
            case 4: return "poison";
            default: return "other";
        }
    }

    char const* PolicyName(DispelPolicy policy)
    {
        switch (policy)
        {
            case DispelPolicy::Auto:        return "auto";
            case DispelPolicy::Now:         return "now";
            case DispelPolicy::Never:       return "never";
            case DispelPolicy::AfterCombat: return "after-combat";
        }
        return "?";
    }
}
}
