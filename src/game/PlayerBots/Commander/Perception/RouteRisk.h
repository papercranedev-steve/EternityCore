/*
 * PartyBot Commander layer 1 (eyes): does walking straight from A to B pull another pack?
 * Pure geometry on WorldModel::packs and ::patrols (no navmesh), for the brain: a melee bot or
 * the tank sent to an enemy walks there. The cerebellum's live check is Behaviors/SafeRoute.h
 * (pathfinding, line of sight, wander circles).
 *
 * A creature counts when it is aggressive (not neutral, not CREATURE_FLAG_EXTRA_NO_AGGRO, not
 * tapped by someone else) and the segment comes within reach + MARGIN of it (2D). A creature
 * whose circle already holds A is left out: the bot stands there and nothing came.
 * Known limit: straight line only, wandering not included (the reach is the aggro range).
 */

#ifndef MANGOS_COMMANDER_ROUTE_RISK_H
#define MANGOS_COMMANDER_ROUTE_RISK_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace RouteRisk
    {
        static constexpr float MARGIN = 3.0f;

        bool CrossesIdle(WorldModel const& world, Position3 const& from, Position3 const& to);
    }
}

#endif
