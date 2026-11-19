/*
 * World population bots: a spot near a place that no player can see (user 2026-11-18: bots
 * never appear or vanish in front of the player). Used by the city and town directors to log a
 * bot in out of sight and walk it to its spot, or walk it away before it logs out.
 * World thread only.
 */

#ifndef MANGOS_POPULATION_HIDDEN_SPOT_H
#define MANGOS_POPULATION_HIDDEN_SPOT_H

#include "Common.h"
#include "Population/PopulationTypes.h"
#include <vector>

class Map;

namespace Population
{
    class Observers;

    // The nearest of `known` (where people stand or walk anyway: reachable) within walking
    // distance of x/y that nobody sees, else open ground around x/y on about the same level.
    // False: none (the bot should not appear / vanish now).
    bool PickHiddenSpot(Map* map, std::vector<Waypoint> const& known, float x, float y, float z,
        Observers const& observers, Waypoint& out);
}

#endif
