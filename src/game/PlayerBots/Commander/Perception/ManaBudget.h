/*
 * PartyBot Commander: healer mana budget, pure arithmetic (no game objects).
 * Used by Behaviors/ManaBudgetBehavior. Design notes: combat scenario 9 (Zul'Farrak stairs,
 * T+40s mana estimate) and scenario 13 section 1.8 (mana ~ damage x 0.6).
 *
 *   T  = seconds the fight still lasts (Value::FightRemainingSec: current enemies only)
 *   H  = healing still needed by this healer
 *      = max(0, incomingDps * T - headroom) / healers
 *        headroom: health above HEADROOM_FLOOR_PCT on the members being hit right now.
 *        That much damage can land without a heal; the party eats after the fight.
 *   E  = healing per mana of this healer's direct heals (average over its spell list)
 *   N  = H / (E * (1 - OVERHEAL))           mana needed
 *   M  = current mana
 *   tension = N / M                           <= 1: enough mana, > 1: throttle
 *
 * Limits: mana regeneration during the fight (spirit, 5 second rule, Mana Tide, potions) is
 * not counted, so the tension is on the high side. T only knows current enemies: waves that
 * have not arrived (scenario 9) are not in it, so the tension is low at the start of a wave.
 */

#ifndef MANGOS_COMMANDER_MANA_BUDGET_H
#define MANGOS_COMMANDER_MANA_BUDGET_H

#include "Common.h"

namespace Commander
{
namespace ManaBudget
{
    // Healing lost to overheal and to heals landing after the damage (scenario 13, 1.8).
    static constexpr float OVERHEAL = 0.15f;
    // Members being hit may drop to this fraction of max health before the fight ends.
    static constexpr float HEADROOM_FLOOR_PCT = 0.5f;
    // Healing per mana when the healer has no usable direct heal to measure
    // (same as CastValue::HEAL_PER_MANA).
    static constexpr float FALLBACK_HEAL_PER_MANA = 2.0f;
    // Saved mana is worth this fraction of the healing it could buy later: the fight estimate
    // is rough and the mana may not be needed after all.
    static constexpr float SAVED_MANA_DISCOUNT = 0.5f;

    // Heal ceilings (health percent at or above which a heal is skipped), by tension.
    // Linear between the points, flat after the last one. 100 = no ceiling.
    //   tension   1.0   1.5   2.0
    //   others    100    70    55
    //   tanks     100   100    70
    static constexpr float CEILING_OTHER_AT_1_5 = 70.0f;
    static constexpr float CEILING_OTHER_AT_2_0 = 55.0f;
    static constexpr float CEILING_TANK_AT_2_0 = 70.0f;

    // Low mana warning (not sent yet, see ManaBudgetBehavior.h): mana below this and the fight
    // lasting longer than WARN_TENSION x the time the mana lasts (that is tension > WARN_TENSION).
    static constexpr float WARN_MANA_PCT = 20.0f;
    static constexpr float WARN_TENSION = 1.5f;

    // H for one healer.
    float NeededHealing(float incomingDps, float fightSec, float headroom, uint32 healers);

    // N.
    float NeededMana(float neededHealing, float healPerMana);

    // N / M. Large when out of mana and something still needs healing, 0 when nothing does.
    float Tension(float neededMana, float mana);

    // Health percent at or above which a heal on this member is skipped (100 = no ceiling).
    float HealCeilingPct(float tension, bool tankLike);

    // 0 at tension 1, rising to 1 at tension 2: how scarce mana is.
    float Scarcity(float tension);

    // Health the saved mana is expected to heal later:
    // mana * healPerMana * SAVED_MANA_DISCOUNT * Scarcity(tension).
    float SavedManaHealth(float mana, float healPerMana, float tension);

    bool WantsLowManaWarning(float manaPct, float tension);
}
}

#endif
