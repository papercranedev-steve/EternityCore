/*
 * PartyBot Commander layer 1 (eyes): how far a bot can walk towards a far point before an idle
 * creature would see it (the push mode's legs, PushPlanner).
 *
 * The navmesh path from the walker to the goal is followed for at most `maxYd`, sampled every
 * SafeRoute::PATH_SAMPLE_STEP against the idle creatures around the walker (SafeRoute,
 * IDLE_MARGIN, wander circles; circles the walker already stands in only count closer to their
 * home: IdleCreatures::LeavingFrom). It stops STOP_BACK_YD before the first sample one of them
 * would attack: the pack there is the next pull, not a walk-in.
 *
 * Facts only, no choice. Map thread of the walker: pathfinds and reads creatures.
 */

#ifndef MANGOS_COMMANDER_ADVANCE_ROUTE_H
#define MANGOS_COMMANDER_ADVANCE_ROUTE_H

#include "Commander/Perception/WorldModel.h"

class Player;

namespace Commander
{
namespace AdvanceRoute
{
    // Stop this far (along the path) before the first sample an idle creature covers.
    float const STOP_BACK_YD = 3.0f;
    // Idle creatures are scanned this far beyond the leg.
    float const IDLE_SCAN_MARGIN = 30.0f;

    struct Leg
    {
        bool path = false;          // a complete navmesh path to the goal (or no navmesh: straight)
        Position3 end;              // where the walk stops
        float yd = 0.0f;            // walk to `end` along the path
        float goalYd = 0.0f;        // the whole path to the goal
        bool reachesGoal = false;   // `end` is the goal: nothing in the way within maxYd
        ObjectGuid blocker;         // the idle creature the walk stopped before, empty = none
    };

    Leg Toward(Player* walker, Position3 const& goal, float maxYd);
}
}

#endif
