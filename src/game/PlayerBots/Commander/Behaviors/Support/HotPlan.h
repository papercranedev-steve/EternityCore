/*
 * PartyBot Commander: HoTs and the spell that eats them (druid Rejuvenation, Swiftmend), pure
 * arithmetic for HealPlanBehavior (user 2026-11-15).
 *
 * HoT rank (PickHot): the healer's HoT goes on a member being hit that has none of the
 * healer's own (one at a time, never refreshed early). Each rank fills
 *   need = missing health + damage expected over its duration x HealSpells::HOT_INCOMING_SHARE
 * and qualifies when it overheals at most HealPlan::OVERHEAL_MAX of its total and heals at
 * least MIN_RANK_SHARE of the biggest rank (no tiny ranks for a scratch). Of those: the
 * biggest when the healer can eat the HoT later (Swiftmend heals for the HoT's ticks: a higher
 * rank is a bigger free heal), else the least mana per health healed (ties: the bigger one).
 *
 * Eating a HoT (Swiftmend). It eats the eatable HoT with the least time left (anyone's,
 * spell_druid.cpp) and heals for its ticks x HotConsumeTicks now; the ticks it still had are
 * lost.
 *   emergency  ready: cast, it lands at once (before Nature's Swiftness and the heals).
 *   free       ready, the last tick is still to come (ticksLeft <= 1) and the target misses at
 *              least heal x (1 - OVERHEAL_MAX): nearly the whole heal for the spell's mana.
 *   lift       ready, two ticks left, the target needs a lift or chain now and misses as much:
 *              it fills the deficit instead of a cast-time heal.
 * Planning (WaitDeficit): two ticks or less left and the spell ready by the last tick: the
 * target's lift waits until it misses heal x (1 - OVERHEAL_MAX) (HealPlan::Target::
 * waitDeficit, never past the safe deficit), so a cast-time heal does not fill the gap the
 * eat would fill. After eating, the HoT is gone and the healer puts a new one on (no own HoT).
 */

#ifndef MANGOS_COMMANDER_HOT_PLAN_H
#define MANGOS_COMMANDER_HOT_PLAN_H

#include "Common.h"
#include "Commander/Behaviors/Support/HealPlan.h"
#include <vector>

namespace Commander
{
namespace HotPlan
{
    // A rank below this share of the biggest one is not worth its global cooldown (and the
    // five second rule it restarts): the target gets no HoT rather than a tiny one.
    static constexpr float MIN_RANK_SHARE = 0.4f;

    struct HotOption
    {
        float total = 0.0f;     // all ticks
        float need = 0.0f;      // health it can fill over its duration (header)
        float mana = 0.0f;
    };

    // Index of the rank to cast, -1 = none fits.
    int PickHot(std::vector<HotOption> const& options, bool preferBig);

    // The HoT the eat spell would take off the target.
    struct Eat
    {
        bool known = false;         // an eatable HoT is on the target and the bot has the spell
        bool ready = false;         // castable now (cooldown, mana, range)
        float heal = 0.0f;          // the eat spell's heal (the HoT's ticks, bonuses)
        float lost = 0.0f;          // ticks the HoT still had
        uint32 ticksLeft = 0;
        float untilLastTickSec = 0.0f;
        float cooldownSec = 0.0f;   // 0 = off cooldown
    };

    enum class EatUse : uint8
    {
        None,
        Emergency,
        Free,
        Lift,
    };

    EatUse Decide(Eat const& e, float missing, HealPlan::Need need);
    // The lift waits for this deficit (HealPlan::Target::waitDeficit), 0 = none.
    float WaitDeficit(Eat const& e);
}
}

#endif
