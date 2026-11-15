/*
 * PartyBot Commander: HoTs and the spell that eats them (druid Rejuvenation, Swiftmend), pure
 * arithmetic for HealPlanBehavior (user 2026-11-15).
 *
 * HoT rank (PickHot, user 2026-11-15): the healer's HoT goes on a member being hit that has
 * none of the healer's own (one at a time, never refreshed early). Each rank fills
 *   need = missing health + damage expected over its duration x MATCH_SHARE
 * and qualifies when it overheals at most HealPlan::OVERHEAL_MAX of its total. Of those:
 *   a party member   the biggest: a HoT that keeps up with the damage or a little below it, so
 *                    the target needs fewer casts. Light damage: a mid rank; heavy damage: the
 *                    top rank once even it does not overheal, the casts (and Swiftmend before
 *                    the last tick) fill the rest.
 *   a pet            (not holding an enemy for the party) the least mana per health healed
 *                    (ties: the bigger one): a low rank.
 * A tank (role tank or an off-tank, doctrine OffTank.h) also needs at least TANK_MIN_RANK_SHARE
 * of the biggest rank: the lowest ranks (Rejuvenation 1-2 with healing gear) heal too little
 * against a tank's damage to be worth the global cooldown. Out of combat (no enemy) this
 * module does not heal; CombatBotBaseAI::ChooseHeal takes the cheapest rank that fits, so the
 * low ranks top the party up there.
 * Swiftmend no longer asks for the biggest rank (until 2026-11-15): with healing gear the top
 * Rejuvenation heals about half as much per mana as rank 4 (Xisu: 3.6 against 5.6), and the
 * eat it prepared often never came (strat-20261115-231212).
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
    // On a tank a rank below this share of the biggest one is not worth its global cooldown:
    // it gets no HoT rather than a tiny one (Rejuvenation 1-2 out, 3 in with +500 healing).
    static constexpr float TANK_MIN_RANK_SHARE = 0.3f;
    // The share of the expected damage a HoT may cover: a little below it, the casts heal too.
    static constexpr float MATCH_SHARE = 0.8f;

    struct HotOption
    {
        float total = 0.0f;     // all ticks
        float need = 0.0f;      // health it can fill over its duration (header)
        float mana = 0.0f;
    };

    enum class HotTarget : uint8
    {
        Member,     // the biggest rank that fits
        Tank,       // the same, no rank below TANK_MIN_RANK_SHARE
        Pet,        // the least mana per health healed
    };

    // Index of the rank to cast, -1 = none fits.
    int PickHot(std::vector<HotOption> const& options, HotTarget target);

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
