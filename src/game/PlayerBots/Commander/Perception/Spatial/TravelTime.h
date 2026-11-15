/*
 * PartyBot Commander layer 1 (eyes): how long a run from one place in the world to another
 * takes (user, 2026-11-15). For the simulated corpse run (Behaviors/Support/CorpseRun.h) and,
 * later, bots that join the leader from another map.
 *
 * Distances are long (a graveyard to a dungeon entrance: hundreds of yards); the navmesh path
 * finder cuts long paths, so the estimate is the straight 2d line times DETOUR_FACTOR.
 * Dungeons are reached through their entrance on the continent (OutsidePoint). Two places on
 * different continents (a boat, a zeppelin): unknown.
 *
 * Facts only, no decisions. Static data (DBC, area triggers): any thread.
 */

#ifndef MANGOS_COMMANDER_TRAVEL_TIME_H
#define MANGOS_COMMANDER_TRAVEL_TIME_H

#include "Common.h"
#include "SharedDefines.h"

namespace Commander
{
    namespace TravelTime
    {
        // Roads and paths wind around hills, houses and water.
        constexpr float DETOUR_FACTOR = 1.4f;

        // Where `loc` is reached from the open world: for a dungeon or raid map the point outside
        // its entrance (the area trigger that leads out of it, else the map's ghost entrance);
        // `loc` itself on a continent. False when a dungeon has neither.
        bool OutsidePoint(WorldLocation const& loc, WorldLocation& out);

        // Seconds a runner at `speedYdPerSec` needs from `from` to `to` (both through
        // OutsidePoint). < 0: unknown (different continents, no entrance, no speed).
        float Seconds(WorldLocation const& from, WorldLocation const& to, float speedYdPerSec);
    }
}

#endif
