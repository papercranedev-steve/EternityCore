/*
 * PartyBot Commander layer 1 (eyes): the warlock curses that raise the party's damage on an
 * enemy, what each one does (from the spell data), and which one a party member's damage
 * profits from. Tags for the curse assigner (Coordination/Duties/CurseAssigner.h), the curse
 * value (Perception/Values/CurseValue.h) and the world model facts
 * (PartyMemberFacts::curseSpells, EnemyFacts::curses).
 *
 * Spell data (spell_template, build 5086):
 *   Curse of the Elements  MOD_DAMAGE_PERCENT_TAKEN fire + frost (rank 3: +10%), MOD_RESISTANCE -75
 *   Curse of Shadow        MOD_DAMAGE_PERCENT_TAKEN shadow + arcane (rank 2: +10%), MOD_RESISTANCE -75
 *   Curse of Recklessness  MOD_RESISTANCE armor (rank 4: -640), MOD_ATTACK_POWER (+90), no fleeing
 * The resistance part is not counted (creatures have little to lose).
 * One curse per warlock per target in this core; the same curse from two warlocks does not stack.
 *
 * Which curse a member profits from (user 2026-11-15: by class, tanks not counted): mage the
 * Elements; warlock and priest Shadow; warrior, rogue, hunter, paladin, a melee shaman or druid
 * Recklessness. Healers are not counted either (they deal next to no damage); a caster
 * shaman or druid (nature) profits from none.
 */

#ifndef MANGOS_COMMANDER_WARLOCK_CURSES_H
#define MANGOS_COMMANDER_WARLOCK_CURSES_H

#include "Common.h"
#include <vector>

class SpellEntry;

namespace Commander
{
    // In the order the assigner prefers among equal counts (user 2026-11-15).
    enum class CurseKind : uint8
    {
        None,
        Elements,
        Shadow,
        Recklessness,
    };

    struct CurseEffect
    {
        CurseKind kind = CurseKind::None;
        float damagePct = 0.0f;     // damage taken in its schools, 0.10 = +10%
        float armorCut = 0.0f;      // armor it takes off
        float apAdded = 0.0f;       // melee attack power it gives the enemy
        float durationSec = 0.0f;
    };

    namespace WarlockCurses
    {
        // By the spell's (English) name, as PopulateSpellData. None for any other spell.
        CurseKind KindOf(SpellEntry const* spell);
        // KindOf plus the numbers of its effects.
        CurseEffect EffectOf(SpellEntry const* spell);
        // The curse that raises this member's damage (header). role: CombatBotRoles.
        CurseKind Favoured(uint8 classId, uint8 role);
        // Elements, Shadow, Recklessness.
        std::vector<CurseKind> TieOrder();
        char const* Name(CurseKind kind);
    }
}

#endif
