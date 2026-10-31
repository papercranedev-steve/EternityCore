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
                case SPELL_EFFECT_SUMMON:
                    raise(CastCategory::Summon);
                    break;
                default:
                    break;
            }

            if (!IsAuraEffect(effect))
                continue;

            switch (spell->EffectApplyAuraName[i])
            {
                case SPELL_AURA_PERIODIC_HEAL:
                    raise(CastCategory::Heal);
                    break;
                case SPELL_AURA_MOD_FEAR:
                case SPELL_AURA_MOD_CONFUSE:
                case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_MOD_POSSESS:
                case SPELL_AURA_MOD_STUN:
                case SPELL_AURA_MOD_SILENCE:
                case SPELL_AURA_MOD_PACIFY_SILENCE:
                    raise(CastCategory::Control);
                    break;
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_LEECH:
                    raise(CastCategory::Damage);
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
            case CastCategory::Heal:    return "heal";
            case CastCategory::Control: return "control";
            case CastCategory::Summon:  return "summon";
            case CastCategory::Damage:  return "damage";
            case CastCategory::Other:   return "other";
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
}
}
