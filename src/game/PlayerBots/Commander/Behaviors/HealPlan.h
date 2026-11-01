/*
 * PartyBot Commander: heal rhythm of one healer, pure arithmetic (no game objects).
 * Used by Behaviors/HealPlanBehavior. Design: .kiro/docs/2026-11-01-heal-plan.md.
 *
 * Per target:
 *   reserve   health kept for a spike: max(MIN_RESERVE_PCT, biggest recent spike x BURST_SAFETY),
 *             at most MAX_RESERVE_PCT of max health
 *   emergency being hit and (health <= reserve, or time to die < EMERGENCY_TTD_SEC):
 *             the biggest heal that lands in time, mana ignored; one that does not pull aggro
 *             if any lands in time
 *   deficit   missing health at which a lift starts:
 *               min = the heal of the most mana-efficient option (one heal fills it)
 *               max = max health - reserve - incoming x (that heal's cast + one bot tick)
 *               deficit = min + (max - min) x manaWeight
 *             Mana plentiful (weight 0): heal as soon as one efficient heal fits. Mana scarce
 *             (weight 1): let the target sink to the reserve, then lift it in one go and rest
 *             longer (fewer five second rule restarts, more spirit regeneration).
 *   chain     the last heal went to this target and ended just now: keep healing it until the
 *             next heal would overheal by more than OVERHEAL_MAX, then rest.
 *
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
    };

    struct Healer
    {
        float regen = 0.0f;         // mana per second outside the five second rule
        float regenCasting = 0.0f;  // inside it
        float tickSec = 1.0f;       // bot update interval
        float manaWeight = 0.0f;    // ManaWeight: 0 plentiful .. 1 scarce
    };

    struct Option
    {
        float heal = 0.0f;          // expected healing (direct + the counted part of a HoT)
        float mana = 0.0f;
        float castSec = 0.0f;
        bool pulls = false;         // the heal would turn an enemy on the healer
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
        float timeToReserve = NO_LIMIT;  // seconds until health reaches the reserve
    };

    // tension: ManaBudget tension of the fight (0 if unknown). Low mana counts on its own:
    // a short fight at 40% mana still means drinking longer afterwards.
    float ManaWeight(float tension, float manaPct);

    float ReserveHealth(Target const& t);

    Assessment Assess(Target const& t, Healer const& h, std::vector<Option> const& options);

    // Index of the heal to cast, -1 = none. Fills Option::effective.
    int Pick(Target const& t, Healer const& h, Assessment const& a, std::vector<Option>& options);
}
}

#endif
