/*
 * What a damage spell of a combat bot does, from spell data and the caster's real numbers
 * (same approach as HealSpells.h for heals). Used by the class rotations (finishing blows)
 * and the Commander area damage module (area spell vs the single target filler).
 *
 *   damage     per target: direct effects (average base + SpellDamageBonusDone, direct) plus
 *              every tick of periodic damage auras, also ground effects (Blizzard: persistent
 *              area aura), with the DOT bonus. Resistances, armor, crits and the target's
 *              damage taken modifiers are not counted.
 *              Melee spells (2026-11-26, melee area damage): weapon damage effects as
 *              Spell::EffectWeaponDmg adds them (the average main hand hit, normalized for
 *              Whirlwind / Mortal Strike, + the effects' bonus, x weapon percent), and school
 *              damage of a melee spell, both with MeleeDamageBonusDone instead of the spell
 *              power bonus. Bloodthirst's school damage is a share of attack power (its spell
 *              script, spell_warrior.cpp), not its base points.
 *   busy       how long the caster is busy: the channel (Blizzard, Evocation), else the cast
 *              time, at least a global cooldown. (A next swing spell, Heroic Strike, Cleave,
 *              has no global cooldown: still counted as one.)
 *   mana       HealSpells::ManaCost.
 *   power      the cost in the spell's power when that is not mana (rage, energy).
 */

#ifndef MANGOS_DAMAGE_SPELLS_H
#define MANGOS_DAMAGE_SPELLS_H

#include "Common.h"

class SpellEntry;
class Unit;

namespace DamageSpells
{
    constexpr float GCD_SEC = 1.5f;

    // Damage `spell` cast by `caster` does to one `target` it hits.
    float Estimate(Unit* caster, Unit const* target, SpellEntry const* spell);
    // Its direct part only (what lands with the hit; periodic ticks left out).
    float EstimateDirect(Unit* caster, Unit const* target, SpellEntry const* spell);

    // Its periodic damage auras together: damage per tick (with the DOT bonus), tick period
    // and ticks (the shortest period of them; ticks 0 = none).
    struct Periodic
    {
        float perTick = 0.0f;
        uint32 periodMs = 0;
        uint32 ticks = 0;
    };
    Periodic EstimatePeriodic(Unit* caster, Unit const* target, SpellEntry const* spell);
    // Seconds the caster is busy with `spell`.
    float BusySec(Unit const* caster, SpellEntry const* spell);
    // Mana the spell costs `caster` now; 0 for spells that do not use mana.
    float ManaCost(Unit* caster, SpellEntry const* spell);
    // Rage or energy the spell costs `caster` now, in the power's own units (rage x10, as
    // Unit::GetPower); 0 for mana and health spells.
    float PowerCost(Unit* caster, SpellEntry const* spell);
}

#endif
