/*
 * Which heal a combat bot casts on an injured target (CombatBotBaseAI::ChooseHeal).
 * Design: .kiro/docs/2026-11-01-heal-choice.md.
 *
 * Every castable single-target heal the bot knows (all ranks, direct and periodic) is valued
 * on the target with the bot's real numbers:
 *   heal       direct: average base value (base + dice / 2) with the caster's and the target's
 *              healing bonuses (SpellHealingBonusDone / Taken: +healing gear, coefficients,
 *              level penalty of low ranks, talents). Periodic: per tick, DOT bonus, x ticks.
 *   mana       Spell::CalculatePowerCost without a Spell object (no SPELLMOD_COST talents).
 *   cast       cast time; the bot is busy max(cast, GCD_SEC).
 *   effective  direct part: at most the health missing when it lands (missing now + incoming
 *              damage x cast time). Periodic part: at most what the direct part leaves, plus
 *              HOT_INCOMING_SHARE of the damage expected during the HoT.
 *   per mana   effective / mana.
 *   throughput direct: effective / busy time; periodic: effective / duration.
 *
 * Situation of the target: incoming damage per second (CombatBotBaseAI::GetIncomingDps; the
 * Commander's measured value for party bots), health, time to die = health / incoming.
 *
 *  Emergency  incoming > 0 and (health < EMERGENCY_PCT or time to die < EMERGENCY_TTD_SEC):
 *             the biggest direct heal that lands LANDING_MARGIN_SEC before the target dies
 *             (ties within TIE_FRACTION: better per mana). None lands in time: the fastest.
 *  Normal     the heal must keep up: throughput >= required = incoming - buffer / HORIZON_SEC,
 *             buffer = health above SAFE_FLOOR_PCT (negative below it, raising the bar).
 *             Of those, the best per mana (ties: the bigger effective heal). None keeps up:
 *             the highest throughput.
 *
 * So a tank at 85% under steady damage gets the cheapest rank that covers the hole, a tank
 * sinking under 50% gets heals that outpace the damage, and one about to die gets the biggest
 * heal that still lands. Overheal lowers "effective", so ranks bigger than the hole lose.
 *
 * Group heals (area / party targets: Prayer of Healing) are not single-target heals and are
 * left out. Chain Heal stays in; only its first target is counted.
 *
 * Danger (IsInDanger, for absorb shields such as Power Word: Shield): incoming > 0 and the
 * target dies before the fastest castable direct heal lands + SHIELD_MARGIN_SEC, or health is
 * below SHIELD_LOW_PCT. Absorbed damage gives a rage user no rage (Unit::DealDamage rewards
 * rage on the damage after absorbs), so a shield is a crisis tool, not a rotation filler.
 */

#ifndef MANGOS_HEAL_SPELLS_H
#define MANGOS_HEAL_SPELLS_H

#include "Common.h"
#include <vector>

class SpellEntry;
class Unit;

namespace HealSpells
{
    constexpr float GCD_SEC = 1.5f;
    constexpr float SAFE_FLOOR_PCT = 50.0f;
    constexpr float HORIZON_SEC = 10.0f;
    constexpr float EMERGENCY_PCT = 35.0f;
    constexpr float EMERGENCY_TTD_SEC = 4.0f;
    constexpr float LANDING_MARGIN_SEC = 0.5f;
    constexpr float HOT_INCOMING_SHARE = 0.5f;
    constexpr float TIE_FRACTION = 0.05f;
    constexpr float SHIELD_MARGIN_SEC = 1.5f;
    constexpr float SHIELD_LOW_PCT = 25.0f;

    struct Amount
    {
        float direct = 0.0f;        // direct heal effects
        float periodic = 0.0f;      // all ticks of periodic heal auras
        float periodicSec = 0.0f;   // duration of the periodic part
    };

    // Healing `spell` cast by `healer` does on `target` (bonuses included, see header).
    Amount Estimate(Unit* healer, Unit const* target, SpellEntry const* spell);
    // Direct part of `spell` when a script adds `extraBase` to its base value before the
    // bonuses (Swiftmend: the eaten HoT's ticks, spell_druid.cpp, then Spell::EffectHeal).
    float EstimateDirectWithBase(Unit* healer, Unit const* target, SpellEntry const* spell, float extraBase);
    // Mana the spell costs `caster` now; 0 for spells that do not use mana.
    float ManaCost(Unit* caster, SpellEntry const* spell);
    // A heal for one target (not Prayer of Healing and other area / party heals).
    bool IsSingleTargetHeal(SpellEntry const* spell);

    struct Situation
    {
        float health = 0.0f;
        float maxHealth = 0.0f;
        float incomingDps = 0.0f;
        float extraNeed = 0.0f;     // damage expected on top of the missing health (pre-heal)
    };

    struct Option
    {
        SpellEntry const* spell = nullptr;
        Amount amount;
        float mana = 0.0f;
        float castSec = 0.0f;
        // Filled by Choose.
        float effective = 0.0f;
        float effectiveDirect = 0.0f;
        float perMana = 0.0f;
        float throughput = 0.0f;
    };

    float TimeToDie(Situation const& s);    // seconds; a large number when nothing hits it
    bool IsEmergency(Situation const& s);
    // Valued as in the header; returns the index of the chosen option, -1 if none heals.
    int Choose(Situation const& s, std::vector<Option>& options);
    // `fastestHealSec`: cast time of the fastest castable direct heal (GCD_SEC if none).
    bool IsInDanger(Situation const& s, float fastestHealSec);
}

#endif
