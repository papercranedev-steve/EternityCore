/*
 * PartyBot Commander layer 1 (eyes): what eating and drinking to full now is worth to a party
 * member who is not in combat (user, 2026-11-07: a caster sitting with a drink when the pull
 * starts keeps drinking while the tank's pressure is low, then heals or fights; the Commander
 * owns all resting since, Behaviors/RestBehavior.h). Pure, on WorldModel facts.
 *
 * Game rules behind it: a member nobody has on a threat list is not in combat and may eat and
 * drink; a hit stands it up and puts it in combat, and in combat nobody eats or drinks. So mana
 * not drunk before its first action of the fight is not drunk until the fight ends.
 *
 * Three cases:
 *  - no fight and none planned (after a fight, travelling): resting costs nothing and nothing
 *    else is to be done; Worth::free, the behavior proposes it at Routine tier, unvalued.
 *  - a fight planned (a pull), none on yet: valued with the pull's FightLoad estimate.
 *  - the party fights: valued with the live rates; no eating (heals are the answer then).
 * Valued:
 *   seconds  the longer of: (maxMana - mana) / (drinkManaPerSec + manaRegen; manaRegen alone
 *            while drinking), (maxHealth - health) / foodHealthPerSec
 *   benefit  ManaWorth::Points of maxMana - mana (live fight, or `planned`; its scarcity makes
 *            the drink worth little while the mana is enough for the fight), plus the missing
 *            health in points when eating
 *   cost     healers 0: their heal orders compete with the drink every tick (an emergency heal
 *            is Urgent and comes first). Others: their part of the fight while sitting,
 *            Value::ContributionPerSecAt(incoming) x seconds.
 * Members without the rates (players, no pool for drinking): nothing to rest.
 */

#ifndef MANGOS_COMMANDER_REST_VALUE_H
#define MANGOS_COMMANDER_REST_VALUE_H

#include "Commander/Perception/ManaWorth.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

    namespace RestValue
    {
        // Mana and health at or above this share count as full (resting ends at 100%).
        static constexpr float FULL_SHARE = 0.99f;

        struct Worth
        {
            bool eat = false;       // health below full, eating allowed now
            bool drink = false;     // mana below full
            bool free = false;      // no fight on or planned: rest, nothing else to do
            float sec = 0.0f;
            float mana = 0.0f;
            float health = 0.0f;
            float benefit = 0.0f;
            float cost = 0.0f;
            bool Any() const { return eat || drink; }
            bool Pays() const { return Any() && (free || benefit > cost); }
        };

        // `planned`: the pull's estimate (sec 0 = none), used while no damage comes in yet.
        Worth Of(WorldModel const& world, PartyMemberFacts const& member, ManaWorth::Fight const& planned);
    }
}

#endif
