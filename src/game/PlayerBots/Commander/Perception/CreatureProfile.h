/*
 * PartyBot Commander layer 1: what a creature can do, derived from world data.
 * Design: .kiro/docs/2026-11-01-value-scoring.md sections 4.2 and 4.3.
 * Built from static data only (creature_template, creature_spells, creature_ai_events).
 */

#ifndef MANGOS_COMMANDER_CREATURE_PROFILE_H
#define MANGOS_COMMANDER_CREATURE_PROFILE_H

#include "Common.h"
#include <string>
#include <vector>

struct CreatureInfo;

namespace Commander
{
    enum class SpellSource : uint8
    {
        SpellList,  // creature_spells (spell_list_id)
        EventAI,    // cast action in creature_ai_events / creature_ai_scripts
        OnHit,      // triggered by a passive "proc trigger spell" aura in creature_template.auras
    };

    struct CreatureSpellRef
    {
        uint32 spellId = 0;
        SpellSource source = SpellSource::SpellList;
        uint8 castTarget = 0xFF;        // ScriptTarget (creature_spells only), 0xFF = unknown
        uint32 repeatMinMs = 0;
        uint32 repeatMaxMs = 0;
        uint32 viaAuraId = 0;           // OnHit: the passive aura that triggers it
        uint32 procChance = 0;          // OnHit: percent
        uint16 castFlags = 0;           // creature_spells.castFlags (CF_MAIN_RANGED_SPELL, ...)
        uint32 targetParam1 = 0;        // creature_spells.targetParam1/2 (heal: radius / health pct)
        uint32 targetParam2 = 0;
        uint32 initialMinMs = 0;        // first cast after the fight starts
        uint32 initialMaxMs = 0;
        float healthPct = 0.0f;         // EventAI HP event: cast when health drops to this (0 = not HP-bound)
        // Put back on the target soon after a dispel: on-hit procs, and spell list
        // entries that repeat every few seconds.
        bool reapplies = false;
    };

    // custom_partybot_creature_override row. Defaults = no change.
    struct CreatureOverride
    {
        float interruptMult = 1.0f;     // x interrupt benefit for every cast of this creature
        float tauntMult = 1.0f;         // x taunt benefit; 0 = never taunt it off someone
        std::string comment;
    };

    struct CreatureProfile
    {
        uint32 entry = 0;
        std::string name;
        uint32 rank = 0;
        uint32 levelMin = 0;
        uint32 levelMax = 0;
        uint32 unitClass = 1;
        float damageMultiplier = 1.0f;
        uint32 baseAttackTimeMs = 2000;
        bool tauntImmune = false;       // CREATURE_IMMUNITY_TAUNT
        uint32 mechanicImmuneMask = 0;
        float fleeAtHealthPct = 0.0f;   // EventAI HP event with a flee action, 0 = does not flee
        uint32 schoolImmuneMask = 0;    // creature_template.school_immune_mask (1 << SPELL_SCHOOL_x)
        // Stands back and casts its main ranged spell; only walks in when that cast fails
        // (no line of sight, out of range, school locked, silenced). CreatureAI.cpp DoSpellsListCasts.
        uint32 mainRangedSpellId = 0;
        // Boss or scripted encounter: world boss rank, C++ boss_ script, EventAI that sets
        // instance data, or the override table. Pull/clear do not pick it on their own.
        bool boss = false;
        // EventAI data is used only when ai_name is EventAI; a C++ script ignores those rows.
        bool scripted = false;
        bool noAggro = false;           // CREATURE_FLAG_EXTRA_NO_AGGRO
        std::vector<CreatureSpellRef> spells;
        bool hasOverride = false;
        CreatureOverride override;

        CreatureSpellRef const* FindSpell(uint32 spellId) const;
        // True if this creature puts the spell back on its victim soon after a dispel.
        bool Reapplies(uint32 spellId) const;
    };

    // Spell list entries that repeat this often on average count as "reapplied" (value-scoring 4.2).
    // Average, not maximum: Defias Captive's Infected Wound repeats every 1-9 s (5 s on average),
    // with the maximum rule the healer dispelled it 14 times in 35 s and healed nobody
    // (Stockade test run, 2026-11-01).
    static uint32 const REAPPLY_REPEAT_MAX_MS = 6000;

    inline bool RepeatsSoon(uint32 repeatMinMs, uint32 repeatMaxMs)
    {
        return repeatMaxMs > 0 && (repeatMinMs + repeatMaxMs) / 2 <= REAPPLY_REPEAT_MAX_MS;
    }

    namespace CreatureProfiles
    {
        CreatureProfile Build(CreatureInfo const* info);

        // Average melee damage per second at this level, before armor
        // (same formula as Creature::SelectLevel, rate config included).
        float EstimateMeleeDps(CreatureProfile const& profile, uint32 level);
        float EstimateMeleeHit(CreatureProfile const& profile, uint32 level);

        char const* SourceName(SpellSource source);
    }
}

#endif
