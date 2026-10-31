/*
 * PartyBot Commander layer 1: groups nearby out-of-combat creatures into packs.
 *
 * - Creatures with the same creature_groups leader are one pack (Stratholme has 366 member rows).
 * - Creatures without a group record join any creature within LINK_DISTANCE (Stockade: only
 *   4 of 93 creatures have a record). Design doc 5.4 says 10-15 yards; 12 until tested.
 * - Two different creature_groups packs are never merged, even through an ungrouped creature.
 *
 * Pure function on plain data.
 */

#ifndef MANGOS_COMMANDER_PACK_RESOLVER_H
#define MANGOS_COMMANDER_PACK_RESOLVER_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
namespace PackResolver
{
    float const LINK_DISTANCE = 12.0f;

    // Result is sorted by distance from `leaderPos` to the pack center, nearest first.
    std::vector<PackFacts> Resolve(std::vector<PackCreature> const& creatures, Position3 const& leaderPos,
                                   float linkDistance = LINK_DISTANCE);

    float Distance(Position3 const& a, Position3 const& b);
}
}

#endif
