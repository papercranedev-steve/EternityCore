/*
 * What a damage spell of a combat bot does, from spell data and the caster's real numbers
 * (same approach as HealSpells.h for heals). Used by the class rotations (finishing blows)
 * and the Commander area damage module (area spell vs the single target filler).
 *
 *   damage     per target: direct effects (average base + SpellDamageBonusDone, direct) plus
 *              every tick of periodic damage auras, also ground effects (Blizzard: persistent
 *              area aura), with the DOT bonus. Resistances, crits and the target's damage
 *              taken modifiers are not counted.
 *   busy       how long the caster is busy: the channel (Blizzard, Evocation), else the cast
 *              time, at least a global cooldown.
 *   mana       HealSpells::ManaCost.
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
}

#endif
