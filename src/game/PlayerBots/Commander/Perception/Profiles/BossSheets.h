/*
 * PartyBot Commander layer 1: hand-written boss data (dungeon boss doc
 * .kiro/docs/2026-11-28-dungeon-boss-encounters.md, gaps G1 and G12; encounter layers doc step 4).
 *
 * Two tables, both read by CreatureProfiles::Build:
 *
 *  Boss list   every dungeon boss by entry (user 2026-11-28: a hand-written list, steadier than
 *              rules). CreatureProfile::boss = world boss rank, a C++ boss_ script, or this
 *              list. The rule "EventAI that sets instance data" it replaces also marked trash
 *              (Tomb Fiend, Grim Patron, Black Guard Sentry, Arugal's Voidwalker) and missed
 *              bosses with spell lists only (Arugal, Hazzas, Rattlegore). Rare elites a
 *              dungeon guide lists are in; pets of bosses and quest NPCs are not.
 *
 *  Ability sheets  what a C++ boss script casts on its own timers, written like creature_spells
 *              rows (the script's numbers: BossAbility). CreatureProfiles::Build adds each row
 *              as a profile spell of source Sheet, so everything that reads profile spells
 *              (HazardZones, CastValue's expected casts, Tags, ThreatWipe, FocusAssigner, the
 *              EventAI-style enrage forecast) sees them like a spell list; SheetTimers
 *              (Forecast/) estimates their timers for IncomingTimeline and ThreatWipe.
 *              Only what the generic modules should know: one-off mechanics a boss module
 *              handles itself (Anastari's Possess, Thermaplugg's bombs) stay out.
 *
 * How to write a row
 *  - castTarget: who the script aims at (TARGET_T_x of ScriptCommands.h). DoCast(victim) =
 *    TARGET_T_HOSTILE; SelectAttackingTarget(RANDOM, 1) = TARGET_T_HOSTILE_RANDOM_NOT_TOP;
 *    (TOPAGGRO, 1) = TARGET_T_HOSTILE_SECOND_AGGRO. An area spell centred on the caster
 *    (EffectImplicitTarget 22 / 15) is TARGET_T_OWNER_OR_SELF whatever the script passes: the
 *    area is around the boss.
 *  - initial / repeat: the script's timers in ms (Reset() values and the values set after a
 *    cast). repeat 0 = once a fight.
 *  - belowPct: the script casts it only at or below this health (0 = any health).
 *  - flags: BOSS_ABILITY_EVERYONE_LEAVES for an area that must be left by everyone, tank
 *    included (Doan's Detonation: 30 yd, about 750 damage, 5 s cast in his bubble).
 *
 * Static data, read-only after start: safe from every map thread.
 */

#ifndef MANGOS_COMMANDER_BOSS_SHEETS_H
#define MANGOS_COMMANDER_BOSS_SHEETS_H

#include "Common.h"
#include <vector>

namespace Commander
{
    enum BossAbilityFlags : uint8
    {
        // Every member leaves its area, the tank too while it is being cast (HazardZones).
        BOSS_ABILITY_EVERYONE_LEAVES = 0x1,
    };

    struct BossAbility
    {
        uint32 spellId = 0;
        uint8 castTarget = 0;
        uint32 initialMinMs = 0;
        uint32 initialMaxMs = 0;
        uint32 repeatMinMs = 0;     // 0 = once a fight
        uint32 repeatMaxMs = 0;
        float belowPct = 0.0f;      // only at or below this health, 0 = any
        uint8 flags = 0;            // BossAbilityFlags
    };

    struct BossSheet
    {
        uint32 entry = 0;
        std::vector<BossAbility> abilities;
    };

    namespace BossSheets
    {
        // On the boss list (dungeon bosses; world boss rank and boss_ scripts are not listed).
        bool IsListedBoss(uint32 entry);
        // The ability sheet of a C++ scripted boss, null if none.
        BossSheet const* Find(uint32 entry);
    }
}

#endif
