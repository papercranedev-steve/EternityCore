/*
 * PartyBot Commander layer 1: what a spell does, derived from spell data.
 * Design: .kiro/docs/2026-11-01-value-scoring.md section 4.1.
 * Pure functions on SpellEntry (static data); no game objects.
 */

#ifndef MANGOS_COMMANDER_SPELL_PROFILE_H
#define MANGOS_COMMANDER_SPELL_PROFILE_H

#include "Common.h"
#include "Commander/Perception/SpellTags.h"
#include <string>

class SpellEntry;

namespace Commander
{
    // Hand-written dispel decision (custom_partybot_spell_override.dispel).
    enum class DispelPolicy : uint8
    {
        Auto        = 0,    // decided by the value of the debuff
        Now         = 1,    // always dispel at once
        Never       = 2,    // never dispel
        AfterCombat = 3,    // only out of combat
    };

    // custom_partybot_spell_override row. Defaults = no change.
    struct SpellOverride
    {
        float interruptMult = 1.0f;     // x interrupt benefit; 0 = never interrupt
        DispelPolicy dispel = DispelPolicy::Auto;
        std::string comment;
    };

    struct SpellProfile
    {
        uint32 spellId = 0;
        CastCategory category = CastCategory::Other;
        uint32 castMs = 0;              // base cast time, 0 = instant
        uint32 durationMs = 0;          // aura or channel duration, 0 = none
        float radius = 0.0f;            // largest effect radius, 0 = single target
        bool channeled = false;
        bool interruptible = false;     // same rule as Spell::EffectInterruptCast
        bool harmful = false;           // negative for the target
        bool controls = false;          // fear, stun, charm, sleep, silence, ... on the target (not on the caster)
        bool breaksOnDamage = false;    // AURA_INTERRUPT_DAMAGE_CANCELS: sleep / sap style, the next hit ends it
        uint8 school = 0;               // spell_template.School (SpellSchools), what an interrupt locks
        uint8 dispel = 0;               // DispelType: 1 magic, 2 curse, 3 disease, 4 poison
        bool hasOverride = false;
        SpellOverride override;
    };

    // Average amounts for one cast, before the target's resistances and armor.
    struct SpellAmounts
    {
        float damage = 0.0f;            // direct damage (weapon effects include one melee hit)
        float periodicDamage = 0.0f;    // all ticks over the full duration
        float heal = 0.0f;
        float periodicHeal = 0.0f;
        float controlSec = 0.0f;        // how long the target loses control
        float mana = 0.0f;              // mana burned or drained (target's mana is not capped here)
        bool healsToFull = false;       // SPELL_EFFECT_HEAL_MAX_HEALTH: heal = target's missing health
        uint32 summonEntry = 0;         // first creature summon effect: creature entry (EffectMiscValue)
        float summonCount = 0.0f;       // creatures of that entry per cast (effect value, at least 1)

        float TotalDamage() const { return damage + periodicDamage; }
        float TotalHeal() const { return heal + periodicHeal; }
    };

    namespace SpellProfiles
    {
        SpellProfile Build(SpellEntry const* spell);

        // Same scaling as SpellCaster::CalculateSpellEffectValue: per-level points clamped to
        // the spell's level range, average of the dice, and SPELL_ATTR_SCALES_WITH_CREATURE_LEVEL.
        // `meleeHit` = the caster's average melee hit, used by weapon damage effects.
        SpellAmounts Estimate(SpellEntry const* spell, uint32 casterLevel, float meleeHit);

        char const* DispelName(uint8 dispel);
        char const* PolicyName(DispelPolicy policy);
    }
}

#endif
