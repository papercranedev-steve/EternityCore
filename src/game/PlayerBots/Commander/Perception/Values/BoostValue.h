/*
 * PartyBot Commander layer 1 (eyes): what a cooldown cast on a party member (PartyMemberFacts::
 * allyBoosts, ClassKit::AllyBoosts) is worth on one target, in points (cooldown doc
 * .kiro/docs/2026-11-14-cooldowns.md 4). Consequences only; BoostAssigner picks.
 *
 *   Mana (Innervate) on a healer (HealerManaValue::Counts: a bot that heals the team, or a
 *     player in the healer role; user 2026-11-26), not boosted already, in range:
 *     gain = (manaRegen x INNERVATE_REGEN_MULT + manaRegen - manaRegenCasting) x duration
 *            (+400% regeneration, all of it while casting: spell 29166 auras 110 / 134)
 *     only when the mana it misses is at least the gain (nothing is wasted: the boost waits
 *     for a big gap, like the heal boosts); benefit = ManaWorth::Points(target, gain) +
 *     HealerManaValue::DeathAvoided(target, gain / duration, duration) (a healer running dry
 *     before the fight ends loses the tank, with nobody to take over the team; user 2026-11-25)
 *   Power (Power Infusion) on a member who deals spell damage or heals (a healer, a ranged
 *     caster that is not a hunter), in combat, with no Power Infusion / Arcane Power up:
 *     benefit = Value::ContributionPerSec(target) x POWER_SHARE x duration
 *   PartyMana (Mana Tide Totem) for the caster itself: every member with a mana pool within
 *     its radius: benefit = sum ManaWorth::Points(member, min(missing, manaPerTarget));
 *     only when the caster's own share or the healers' would not be wasted (the sum of what
 *     is missing is at least PARTY_MANA_MIN_SHARE of what it gives)
 *   cost = LostAction(caster, GCD) when it starts the global cooldown + ManaWorth(caster's
 *          mana cost) + Value::CooldownPoints over the fight left for cooldowns longer than
 *          BurstValue::SHORT_COOLDOWN_SEC (Innervate 6 min; Power Infusion 3 min, Mana Tide
 *          5 min are back for the next pull)
 *          + FormExitCost::Of(caster) when it is cast after leaving a form (AllyBoostAbility::
 *          leavesForm: a druid tank's Innervate from bear form); not offered when that says
 *          the caster cannot leave now
 * Only while the party fights (the caller). Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_BOOST_VALUE_H
#define MANGOS_COMMANDER_BOOST_VALUE_H

#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace BoostValue
    {
        // Untested guesses.
        static constexpr float INNERVATE_REGEN_MULT = 4.0f;
        static constexpr float POWER_SHARE = 0.2f;
        static constexpr float PARTY_MANA_MIN_SHARE = 0.5f;

        enum Kind : uint8 { MANA = 0, POWER = 1, PARTY_MANA = 2 };   // = ClassKit::AllyBoostKind

        // `target` = the member it goes on (PartyMana: the caster). Option::benefit / cost;
        // no benefit = not offered.
        RescueValue::Option Of(WorldModel const& world, PartyMemberFacts const& caster, AllyBoostAbility const& a,
                               PartyMemberFacts const& target);
    }
}

#endif
