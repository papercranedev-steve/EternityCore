#include "Commander/Perception/SpellTags.h"
#include "SpellEntry.h"

namespace Commander
{
namespace SpellTags
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
    }

    bool IsControlEffect(SpellEntry const* spell, uint32 i)
    {
        return IsAuraEffect(spell->Effect[i]) && IsControlAura(spell->EffectApplyAuraName[i]) &&
               spell->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER;
    }

    bool IsCreatureSummonEffect(uint32 effect)
    {
        switch (effect)
        {
            case SPELL_EFFECT_SUMMON:
            case SPELL_EFFECT_SUMMON_WILD:
            case SPELL_EFFECT_SUMMON_GUARDIAN:
            case SPELL_EFFECT_SUMMON_PET:
            case SPELL_EFFECT_SUMMON_POSSESSED:
            case SPELL_EFFECT_SUMMON_TOTEM:
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT1:
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT2:
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT3:
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT4:
            case SPELL_EFFECT_SUMMON_DEMON:
                return true;
            default:
                return false;
        }
    }

    CastCategory Categorize(SpellEntry const* spell)
    {
        if (!spell)
            return CastCategory::Other;

        CastCategory best = CastCategory::Other;
        auto raise = [&best](CastCategory c) { if (uint8(c) > uint8(best)) best = c; };

        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            uint32 const effect = spell->Effect[i];
            // Every creature summon effect (wild, guardian, totem, ...), not only SUMMON:
            // summon spells like the Remote Control Golem were "other" and never interrupted.
            if (IsCreatureSummonEffect(effect))
                raise(CastCategory::Summon);

            switch (effect)
            {
                case SPELL_EFFECT_HEAL:
                case SPELL_EFFECT_HEAL_MAX_HEALTH:
                    raise(CastCategory::Heal);
                    break;
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                    raise(CastCategory::Damage);
                    break;
                // Mana burn / drain: valued by the healing the lost mana would have done.
                case SPELL_EFFECT_POWER_BURN:
                case SPELL_EFFECT_POWER_DRAIN:
                    if (spell->EffectMiscValue[i] == POWER_MANA)
                        raise(CastCategory::ManaDrain);
                    break;
                default:
                    break;
            }

            if (IsControlEffect(spell, i))
            {
                raise(CastCategory::Control);
                continue;
            }
            if (!IsAuraEffect(effect))
                continue;

            switch (spell->EffectApplyAuraName[i])
            {
                case SPELL_AURA_PERIODIC_HEAL:
                    raise(CastCategory::Heal);
                    break;
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_LEECH:
                    raise(CastCategory::Damage);
                    break;
                case SPELL_AURA_PERIODIC_MANA_LEECH:
                    raise(CastCategory::ManaDrain);
                    break;
                default:
                    break;
            }
        }
        return best;
    }

    char const* CategoryName(CastCategory category)
    {
        switch (category)
        {
            case CastCategory::Heal:      return "heal";
            case CastCategory::Control:   return "control";
            case CastCategory::Summon:    return "summon";
            case CastCategory::ManaDrain: return "mana drain";
            case CastCategory::Damage:    return "damage";
            case CastCategory::Other:     return "other";
        }
        return "?";
    }

    bool IsInterruptible(SpellEntry const* spell, bool channeled, int32 castTimeMs)
    {
        if (!spell || spell->PreventionType != SPELL_PREVENTION_TYPE_SILENCE)
            return false;
        if (channeled)
            return spell->HasChannelInterruptFlag(AURA_INTERRUPT_ACTION_CANCELS);
        return castTimeMs > 0 && spell->HasSpellInterruptFlag(SPELL_INTERRUPT_FLAG_DAMAGE_PUSHBACK);
    }

    bool StopsCasts(SpellEntry const* spell)
    {
        if (!spell)
            return false;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spell->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST)
                return true;
            if (IsAuraEffect(spell->Effect[i]) &&
               (spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_STUN || spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_SILENCE))
                return true;
        }
        return false;
    }

    uint32 StopMechanicMask(SpellEntry const* spell)
    {
        if (!spell)
            return 0;
        uint32 mask = spell->Mechanic ? (1u << (spell->Mechanic - 1)) : 0;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spell->EffectMechanic[i])
                mask |= 1u << (spell->EffectMechanic[i] - 1);
        return mask;
    }

    bool IsStopImmune(SpellEntry const* spell, uint32 mechanicImmuneMask, uint32 schoolImmuneMask)
    {
        if (!spell || spell->HasAttribute(SPELL_ATTR_NO_IMMUNITIES) || spell->IsIgnoringCasterAndTargetRestrictions())
            return false;
        // Creature::IsImmuneToSpell: an immune school blocks the whole harmful spell.
        if ((schoolImmuneMask & (1u << spell->School)) && !spell->IsPositiveSpell())
            return true;
        // Bit convention of Creature::IsImmuneToSpell(Effect): 1 << (mechanic - 1).
        uint32 const mask = StopMechanicMask(spell);
        return mask && (mask & ~mechanicImmuneMask) == 0;
    }
}
}
