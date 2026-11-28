/*
 * PartyBot Commander: heal rhythm of one healer, pure arithmetic (no game objects).
 * Used by Behaviors/Support/HealPlanBehavior. Design: .kiro/docs/2026-11-01-heal-plan.md.
 *
 * Per target:
 *   reserve   health kept for a spike: max(MIN_RESERVE_PCT, biggest recent spike x BURST_SAFETY),
 *             at most MAX_RESERVE_PCT of max health
 *   emergency being hit and (health <= reserve, or time to die < EMERGENCY_TTD_SEC, or the
 *             worst case time to die before the fastest heal started now lands: its cast +
 *             LANDING_MARGIN_SEC + one bot tick; 2026-11-17, the hits one by one; or, with a
 *             lockout of the healer ahead that a heal started now still lands before, before
 *             the first heal after the lockout lands: Healer::NextChanceSec, boss doc G7):
 *             the biggest heal that lands in time, mana ignored; one that does not pull aggro
 *             if any lands in time
 *   deficit   missing health at which a lift starts:
 *               min = the heal of the most mana-efficient option (one heal fills it)
 *               max = max health - reserve - incoming x (that heal's cast + one bot tick)
 *               deficit = min + (max - min) x manaWeight
 *             Mana plentiful (weight 0): heal as soon as one efficient heal fits. Mana scarce
 *             (weight 1): let the target sink to the reserve, then lift it in one go and rest
 *             longer (fewer five second rule restarts, more spirit regeneration).
 *             A heal boost ready (Target::boostDeficit, Inner Focus, Divine Favor): at least
 *             the boosted biggest heal x (1 - OVERHEAL_MAX) when that is at most max
 *             (2026-11-14, Divine Favor 2026-11-15). A HoT Swiftmend is about to eat
 *             (Target::waitDeficit): at least its deficit, at most max.
 *   chain     the last heal went to this target and ended just now: keep healing it until the
 *             next heal would overheal by more than OVERHEAL_MAX, then rest.
 *
 * Emergency-only options (Option::emergencyOnly) count only for an emergency: not for the
 * deficit, a lift, a chain or a non-instant boost.
 * Rank of a lift or chain heal: options that do not pull aggro, overheal at most OVERHEAL_MAX
 * when they land, and lift the missing health within LIFT_MAX_SEC against the incoming damage.
 * Of those the lowest net mana per second over one lift + rest cycle:
 *   cycle deficit Dc = max(deficit, missing), net = heal per second - incoming
 *   cast time tc = Dc / net, rest time tr = Dc / incoming
 *   mana spent = mana per second while casting x tc
 *   regained   = regen inside the rule x (tc + min(5, tr)) + regen outside x max(0, tr - 5)
 *   rate       = (spent - regained) / (tc + tr)
 * Nothing hits the target: the least mana per health healed. None qualifies: the fastest heal
 * that does not overheal; none of those either: no heal (topped up, or every heal pulls aggro).
 * A heal that also heals others (Option::otherHeal, Chain Heal; 2026-11-15): the overheal is
 * judged on all of it, and only effective / (effective + otherEffective) of its mana spent counts
 * for this target: the same mana also bought the rest elsewhere. The lift itself (LIFT_MAX_SEC) is the target's own.
 *
 * Steady healer (Healer::steady, ClassKit::HealsSteadily: paladin, user 2026-11-16; resting
 * regains it little, so the lift + rest cycle only left it idle and favoured one big Holy Light
 * over Flash of Light, strat-20261116-005955):
 *   deficit   the most efficient heal x (1 - OVERHEAL_MAX), compared with the health missing
 *             when that heal lands (missing + incoming x its cast): it starts before the hits,
 *             also on a full target being hit. Mana weight does not delay it. A ready boost
 *             (boostDeficit, Divine Favor; user 2026-11-16: "save up a big one") does, on a
 *             target being hit whose safe deficit holds it: that one sinks until the boosted big
 *             heal fills it, the others get the steady heals. A target nobody hits is not held
 *             (its gap would not grow). A chain waits for the deficit too (the chain only
 *             orders the targets).
 *   rank      the least mana per health healed (no regeneration term) among the ranks that
 *             hold the target: out-heal the damage, or keep it above its reserve for
 *             STEADY_HOLD_SEC while casting them back to back. No "lift it in one go" (user
 *             2026-11-16: two Flash of Light rank 1 instead of one rank 6; a tank at 60% needs
 *             no Holy Light, strat-20261116-215832). Rank 1 while the target is high; as it
 *             sinks toward the reserve the cheaper ranks stop holding it and a bigger one takes
 *             over (Holy Light only when no Flash of Light rank holds). None holds: the fastest.
 *   cancel    a heal that would land mostly as overheal is cancelled just before it lands
 *             (CastWatch, snipe): no mana is spent on a cancelled cast.
 */

#ifndef MANGOS_COMMANDER_HEAL_PLAN_H
#define MANGOS_COMMANDER_HEAL_PLAN_H

#include "Common.h"
#include <vector>

namespace Commander
{
namespace HealPlan
{
    static constexpr float BURST_SAFETY = 1.25f;
    static constexpr float MIN_RESERVE_PCT = 30.0f;
    static constexpr float MAX_RESERVE_PCT = 70.0f;
    static constexpr float LIFT_MAX_SEC = 8.0f;
    static constexpr float STEADY_HOLD_SEC = 10.0f;
    static constexpr float OVERHEAL_MAX = 0.2f;
    static constexpr float EMERGENCY_TTD_SEC = 4.0f;
    static constexpr float LANDING_MARGIN_SEC = 0.5f;
    static constexpr float FIVE_SECOND_RULE_SEC = 5.0f;
    static constexpr float GCD_SEC = 1.5f;
    static constexpr float TIE_FRACTION = 0.05f;
    static constexpr float NO_LIMIT = 1.0e9f;

    struct Target
    {
        float health = 0.0f;
        float maxHealth = 0.0f;
        float intake = 0.0f;        // damage per second expected (Value::TakenDps)
        float burst = 0.0f;         // biggest recent spike (PartyMemberFacts::takenBurst)
        bool chained = false;       // the healer's last heal went here and just ended
        // The lift waits until this much is missing (a HoT about to be eaten by Swiftmend,
        // HotPlan::WaitDeficit), but never past the safe deficit (max health - reserve -
        // incoming during the heal). 0 = no wait.
        float waitDeficit = 0.0f;
        // A heal boost that makes the next heal bigger or free is ready (Inner Focus, Divine
        // Favor; users 2026-11-14 / 2026-11-15): the lift waits until this much is missing, so
        // one boosted big heal (crit counted) fills it. Only when it is within the safe deficit:
        // a target too small for it is lifted as without the boost (until 2026-11-15 the wait
        // was capped at the safe deficit, where PickBoosted then still found the gap too small).
        float boostDeficit = 0.0f;
        // From the hits coming one by one (Perception/Forecast/IncomingTimeline.h, its HoT
        // ticks counted), filled by the caller; < 0 = not known: health / intake.
        //   timeToDie       expected hits
        //   worstTimeToDie  every hit high, the biggest one a crit
        //   timeToReserve   expected hits down to ReserveHealth
        float timeToDie = -1.0f;
        float worstTimeToDie = -1.0f;
        float timeToReserve = -1.0f;
    };

    struct Healer
    {
        float regen = 0.0f;         // mana per second outside the five second rule
        float regenCasting = 0.0f;  // inside it
        float tickSec = 1.0f;       // bot update interval
        float manaWeight = 0.0f;    // ManaWeight: 0 plentiful .. 1 scarce
        // Heals steadily, no lift + rest rhythm (ClassKit::HealsSteadily, paladin; see header).
        bool steady = false;
        // The healer will be controlled from lockoutStartSec to lockoutEndSec (IncomingTimeline
        // Lockout: Ground Tremor, an area fear), < 0 = none: no heal of its lands in between.
        float lockoutStartSec = -1.0f;
        float lockoutEndSec = -1.0f;

        // When a heal of `castSec` started now, or the first one after the lockout, can land
        // last: the next chance to save a target (Assess's outrun, the death penalty).
        float NextChanceSec(float castSec) const;
    };

    struct Option
    {
        float heal = 0.0f;          // expected healing (direct + the counted part of a HoT)
        float mana = 0.0f;
        float castSec = 0.0f;
        bool pulls = false;         // the heal would turn an enemy on the healer
        bool emergencyOnly = false; // only for an emergency (ClassKit::CombatHealUse: Regrowth)
        // Healing on other members (Chain Heal's jumps, Perception/Forecast/ChainHealForecast.h):
        // expected and, of it, what they miss when it lands. Counted in a lift's overheal and
        // mana rate, not in an emergency (only the dying target counts there).
        float otherHeal = 0.0f;
        float otherEffective = 0.0f;
        // Filled by Pick.
        float effective = 0.0f;     // heal capped at the health missing when it lands
    };

    enum class Need : uint8
    {
        None,       // full health, or no usable heal
        Rest,       // hurt, but less than the deficit
        Lift,
        Chain,
        Emergency,
    };

    struct Assessment
    {
        Need need = Need::None;
        float reserve = 0.0f;
        float deficit = 0.0f;
        float timeToDie = NO_LIMIT;
        float worstTimeToDie = NO_LIMIT;
        float timeToReserve = NO_LIMIT;  // seconds until health reaches the reserve
        bool boostHeld = false;          // the deficit is Target::boostDeficit (a ready boost waits)
    };

    // tension: ManaBudget tension of the fight (0 if unknown). Low mana counts on its own:
    // a short fight at 40% mana still means drinking longer afterwards.
    float ManaWeight(float tension, float manaPct);

    float ReserveHealth(Target const& t);

    Assessment Assess(Target const& t, Healer const& h, std::vector<Option> const& options);

    // Index of the heal to cast, -1 = none. Fills Option::effective.
    int Pick(Target const& t, Healer const& h, Assessment const& a, std::vector<Option>& options);

    // A heal boost that is ready (ClassKit::HealBoostSpell, cooldown doc
    // .kiro/docs/2026-11-14-cooldowns.md 7.1). Heal crits are x CRIT_MULT.
    struct Boost
    {
        bool free = false;          // no mana (Inner Focus)
        float critChance = 0.0f;    // added crit chance (Inner Focus 0.25, Divine Favor 1)
        bool instant = false;       // the heal lands at once (Nature's Swiftness)
    };
    static constexpr float CRIT_MULT = 1.5f;

    // The heal to cast with the boost, -1 = keep the boost for later. After Pick (`picked`, its
    // choice; options' `effective` and `pulls` filled). Only heals that do not pull aggro.
    //   instant  only in an emergency the picked heal does not reach in time (it lands at
    //            timeToDie - LANDING_MARGIN_SEC or later, or none was picked): the biggest heal
    //            with a cast time, landing now.
    //   other    the biggest heal; only when the health missing when it lands is at least its
    //            boosted amount (heal x (1 + min(1, critChance) x (CRIT_MULT - 1))) x
    //            (1 - OVERHEAL_MAX): the boost waits for a deficit that one big heal fills
    //            (user 2026-11-14: Inner Focus waits for a big gap, then one big heal). In an
    //            emergency also only when it lands in time; otherwise only for a lift or chain.
    int PickBoosted(Target const& t, Assessment const& a, std::vector<Option> const& options, int picked,
                    Boost const& b);
    // Expected heal of `o` with the boost's crit chance.
    float BoostedHeal(Option const& o, Boost const& b);
}
}

#endif
