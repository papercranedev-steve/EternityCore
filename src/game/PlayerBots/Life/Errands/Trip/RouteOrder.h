/*
 * Bot daily life (PlayerBots/Life), town trips: the order to visit the stops in.
 *
 * Distances are real walks along the navmesh (Shared/WalkLength: Detour's A*, water costs
 * more, closed doors cut the path), measured between every pair of points once. A trip has few
 * stops (at most MAX_STOPS), so every order is tried and the shortest walk from the bot through
 * all stops wins, keeping each stop's `after` (the mailbox after the auction house) and its
 * `last` (after every other stop). Over MAX_STOPS the stops added last go (lowest business). A leg
 * without a full path costs UNREACHABLE_YD on top of its straight distance, so such an order
 * loses to any walkable one; a stop the bot cannot walk to from anywhere is dropped. A
 * `farSpawn` stop (TripPlan.h: its navmesh may not be loaded yet) is never dropped, and its
 * unmeasured legs cost the straight distance.
 */

#ifndef MANGOS_LIFE_TRIP_ROUTE_ORDER_H
#define MANGOS_LIFE_TRIP_ROUTE_ORDER_H

#include "Life/Errands/Trip/TripPlan.h"

namespace Life
{
namespace Trip
{
namespace RouteOrder
{
    constexpr size_t MAX_STOPS = 8;
    constexpr float UNREACHABLE_YD = 1000.0f;

    // Reorders `plan` (the `after` links are kept valid). False: nothing left to visit.
    bool Order(Player const* me, TripPlanStops& plan);
}
}
}

#endif
