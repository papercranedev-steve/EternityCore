/*
 * PartyBot Commander layer 1 (eyes): what one single target damage spell cast now on an enemy
 * is worth, per second the caster is busy with it, so a rotation picks the best of its damage
 * spells each time instead of a fixed order (strat-20261127-000359: a warlock put Corruption,
 * Siphon Life and Immolate on every new enemy first and never cast a Shadow Bolt; the last
 * enemy got all three at 44% to 16% health and died 2-6 s later, one Siphon Life tick of 0).
 * Facts and consequences only; the class rotation casts the best (ClassRotation::CastBestDamage).
 *
 * Death: when the enemy dies, ms after the refresh (DeathMs), the earlier of
 *   FinishForecast::DeathMs   the damage sources on it (measured rates, pending hits, our dots);
 *                             it leaves out what it cannot be sure of (a source that started
 *                             less than DamageDealt::RATE_WINDOW_MS ago, area damage, half of a
 *                             player's), the safe side for "do the others kill it before my hit"
 *   Value::EnemyDyingSec      the party's measured damage shared over the enemies that can be
 *                             hit, while it is being hurt
 * The earlier is the safe side here: periodic damage priced on an enemy that lives longer than
 * it does was the waste.
 *
 * Damage landed (Landed): the direct part when the hit lands before the death, plus the ticks
 * of its periodic part between the landing and the death, at most the enemy's health.
 * `consumes`: a periodic effect of the caster's this spell ends (Conflagrate ends Immolate):
 * its ticks it would still have dealt before the death are taken off.
 * Worth: Value::Points(landed) - ManaWorth::Points(mana), over DamageSpells::BusySec.
 * Not counted: crits, resists, the healing of Siphon Life / Drain Life, threat (the
 * rotation's damage spells are all single target ones the brain already allows on it).
 */

#ifndef MANGOS_COMMANDER_DAMAGE_SPELL_VALUE_H
#define MANGOS_COMMANDER_DAMAGE_SPELL_VALUE_H

#include "Commander/Perception/WorldModel.h"

class Player;
class SpellEntry;
class Unit;

namespace Commander
{
    namespace DamageSpellValue
    {
        // When `enemy` dies, ms after the refresh, within `horizonMs` (0 = not by then).
        // `selfFromMs`: `self`'s own measured rate on it counts from then (FinishForecast::DeathMs).
        uint32 DeathMs(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, uint32 selfFromMs,
                       uint32 horizonMs);

        struct Rating
        {
            float landed = 0.0f;        // damage dealt before the enemy dies
            float busySec = 0.0f;
            float points = 0.0f;        // Points(landed) - mana worth
            float perSec = 0.0f;        // points / busySec
        };

        // `spell` cast by `caster` on `target` now (`nowMs`: the live time, the facts are from
        // the refresh). A target or caster missing from the facts: all 0.
        Rating Rate(Player* caster, WorldModel const& world, Unit const* target, SpellEntry const* spell,
                    uint32 nowMs, SpellEntry const* consumes = nullptr);
    }
}

#endif
