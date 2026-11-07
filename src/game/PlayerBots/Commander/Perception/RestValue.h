/*
 * PartyBot Commander layer 1 (eyes): what drinking to full now is worth to a party member who
 * is not in combat while the party fights or a pull runs (user, 2026-11-07: a caster sitting
 * with a drink when the pull starts keeps drinking while the tank's pressure is low, then
 * heals or fights). Pure, on WorldModel facts.
 *
 * Game rules behind it: a member nobody has on a threat list is not in combat and may drink;
 * a hit stands it up and puts it in combat, and in combat nobody drinks. So mana not drunk
 * before its first action of the fight is not drunk until the fight ends.
 *
 *   seconds  (maxMana - mana) / (drinkManaPerSec + manaRegen; manaRegen alone while drinking)
 *   benefit  ManaWorth::Points of maxMana - mana: the live fight, or before it starts (no
 *            incoming damage yet) `planned`, the pull's FightLoad estimate. The scarcity in it
 *            makes the drink worth little while the mana is enough for the fight.
 *   cost     healers 0: their heal orders compete with the drink every tick (an emergency
 *            heal is Urgent and comes first). Others: their part of the fight while sitting,
 *            Value::ContributionPerSecAt(incoming) x seconds.
 * Members without a drink rate (players, no mana pool): nothing (Worth::Pays false).
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
        // Mana at or above this share of the pool counts as full (drinking ends at 100%).
        static constexpr float FULL_SHARE = 0.99f;

        struct Worth
        {
            float sec = 0.0f;
            float mana = 0.0f;
            float benefit = 0.0f;
            float cost = 0.0f;
            bool Pays() const { return benefit > cost; }
        };

        // `planned`: the pull's estimate (sec 0 = none), used while no damage comes in yet.
        Worth OfDrinking(WorldModel const& world, PartyMemberFacts const& member, ManaWorth::Fight const& planned);
    }
}

#endif
