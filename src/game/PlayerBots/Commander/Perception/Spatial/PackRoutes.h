/*
 * PartyBot Commander layer 1 (eyes): how the leader would walk to each pack (PackFacts::route).
 *
 * The straight distance of PackResolver ranks a pack behind a wall, or on the floor above, as
 * near. Here every pack gets the navmesh walk from the leader to its member nearest the leader:
 * its length, or no way at all, and the first idle creature of another pack the walk comes
 * into the aggro range of (SafeRoute::PathIsSafe against the leader, the pack itself and
 * patrols left out).
 * A pack in a corridor is then nearer than one outside it, and a pack whose way passes another
 * pack is known to be behind it. Also whether the leader sees any of its members (line of
 * sight): a pack in front of him (the active pull stance in a dungeon).
 *
 * Facts only: the clear (ClearPlanner) decides the order. Calculated by RefreshPacks out of
 * combat for the MAX_ROUTED_PACKS nearest packs (one path each); the rest stay Unknown.
 * Map thread of the party: pathfinds and reads creatures.
 */

#ifndef MANGOS_COMMANDER_PACK_ROUTES_H
#define MANGOS_COMMANDER_PACK_ROUTES_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Player;

namespace Commander
{
namespace PackRoutes
{
    // One path per pack, every pack refresh (1 s) out of combat.
    uint32 const MAX_ROUTED_PACKS = 16;
    // Idle creatures are scanned this far beyond the farthest routed pack: their aggro circles
    // reach into the walk.
    float const IDLE_SCAN_MARGIN = 30.0f;

    // Fills `route` of the packs (sorted nearest first by straight distance, PackResolver).
    // Patrols (WorldModel::patrols) are never a blocker: where one is now says nothing about
    // where it is when the party walks there, and the pull planner times the pull around it
    // (patrol wait, patrol first). strat-20261125: three Rockwing Screechers fly waypoint laps
    // over the Stratholme courtyard; one of them was on the walk to every pack, so no pack was
    // in front and the active pull stance stopped every 30 s after 0 packs.
    void Fill(Player* leader, std::vector<PackFacts>& packs, std::vector<PatrolFacts> const& patrols);
}
}

#endif
