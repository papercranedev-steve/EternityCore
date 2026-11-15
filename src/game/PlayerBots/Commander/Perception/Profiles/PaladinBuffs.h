/*
 * PartyBot Commander layer 1 (eyes): which paladin blessing or party aura a spell is. Tags for
 * the blessing assigner (Coordination/Situation/BlessingAssigner.h) and the world model facts
 * (PartyMemberFacts::blessingSpells / auraSpells / blessingsOn / ownAura).
 *
 * Stacking in this core (Unit::RemoveNoStackAurasDueToAuraHolder, SpellEntry.h
 * PerTargetPerCaster / SpellRanksPerTarget): one blessing per paladin per target, one aura per
 * paladin; the same blessing or aura from two paladins does not stack, different ones do.
 * Greater Blessings count as their blessing (same effect, they do not stack either).
 */

#ifndef MANGOS_COMMANDER_PALADIN_BUFFS_H
#define MANGOS_COMMANDER_PALADIN_BUFFS_H

#include "Common.h"

class SpellEntry;

namespace Commander
{
    enum class BlessingKind : uint8
    {
        None,
        Wisdom,
        Might,
        Kings,
        Salvation,
        Light,
        Sanctuary,
    };

    enum class PaladinAuraKind : uint8
    {
        None,
        Devotion,
        Concentration,
        Retribution,
        Sanctity,
        ShadowResistance,
        FrostResistance,
        FireResistance,
    };

    // A judgement debuff whose hits help the party (JudgementAssigner): Judgement of Wisdom
    // gives the hitter mana, of Light health (UnitAuraProcHandler.cpp, CF_PALADIN_JUDGEMENT_OF_
    // WISDOM_LIGHT). It lasts 10 s; only the judging paladin's own melee hits refresh it
    // (Unit.cpp, melee damage path: RefreshHolder of its ALWAYS_HIT paladin auras).
    enum class JudgementKind : uint8
    {
        None,
        Wisdom,
        Light,
    };

    namespace PaladinBuffs
    {
        // By the spell's (English) name, as PopulateSpellData. None for any other spell.
        BlessingKind BlessingOf(SpellEntry const* spell);
        PaladinAuraKind AuraOf(SpellEntry const* spell);
        // The seal judged for it (Seal of Wisdom / Light) or the debuff itself.
        JudgementKind SealOf(SpellEntry const* spell);
        JudgementKind JudgementOf(SpellEntry const* spell);
    }
}

#endif
