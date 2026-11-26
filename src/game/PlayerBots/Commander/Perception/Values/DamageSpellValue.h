/*
 * PartyBot Commander layer 1 (eyes): what one single target damage spell, or a wand shot, cast
 * now on an enemy is worth, per second the caster is busy with it, so a rotation picks the best
 * each time instead of a fixed order (strat-20261127-000359: a warlock put Corruption, Siphon
 * Life and Immolate on every new enemy first and never cast a Shadow Bolt; the last enemy got
 * all three at 44% to 16% health and died 2-6 s later, one Siphon Life tick of 0).
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
 * Damage landed: the direct part when the hit lands before the death, plus the ticks of its
 * periodic part between the landing and the death, at most the enemy's health. `consumes`: a
 * periodic effect of the caster's this spell ends (Conflagrate ends Immolate): its ticks it
 * would still have dealt before the death are taken off.
 * Points: damage is worth what the same damage from the caster's filler is worth: seconds of
 * filler casting (damage / the filler's damage per busy second) at the caster's share of the
 * fight, Points(incoming damage per second) / damage dealers per second, the unit ManaWorth
 * prices a damage dealer's mana in. Worth = points(landed) - the mana's price, over the busy
 * time (DamageSpells::BusySec). The mana's price: ManaWorth::Points; for a caster that taps
 * health into mana (warlock Life Tap) at most the global cooldowns of the taps that pay it back
 * (the user, 2026-11-26: its health has no price, Drain Life brings it back; ManaWorth's tap
 * branch also counts the health, which priced a Shadow Bolt under a wand shot).
 * Wand (RateWand): the ranged weapon's average hit, no mana, busy max(global cooldown, the
 * ranged attack time): each shot fired starts a global cooldown (Spell::cast, as the client).
 * Unknown (`known` false): no facts on the caster or the enemy, no filler damage, nothing
 * hitting the party (incoming 0: every worth is 0); the caller keeps its own order then.
 * Not counted: crits, resists, the healing of Siphon Life / Drain Life, threat (the rotation's
 * damage spells are all single target ones the brain already allows on it).
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
            bool known = false;
            float landed = 0.0f;        // damage dealt before the enemy dies
            float busySec = 0.0f;
            float points = 0.0f;        // worth of the damage - worth of the mana
            float perSec = 0.0f;        // points / busySec
        };

        // `spell` cast by `caster` on `target` now (`nowMs`: the live time, the facts are from
        // the refresh); `filler`: the caster's filler (ClassKit::Filler), the damage unit.
        Rating Rate(Player* caster, WorldModel const& world, Unit const* target, SpellEntry const* spell,
                    SpellEntry const* filler, uint32 nowMs, SpellEntry const* consumes = nullptr);
        // One wand shot of `caster` (`shoot`: Shoot) on `target`.
        Rating RateWand(Player* caster, WorldModel const& world, Unit const* target, SpellEntry const* shoot,
                        SpellEntry const* filler, uint32 nowMs);
    }
}

#endif
