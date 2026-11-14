/*
 * PartyBot Commander layer 1 (eyes): does walking straight from A to B pull another pack?
 * Pure geometry on WorldModel::packs and ::patrols (no navmesh), for the brain: a melee bot or
 * the tank sent to an enemy walks there. The cerebellum's live check is Perception/Spatial/SafeRoute.h
 * (pathfinding, line of sight, wander circles).
 *
 * A creature counts when it is aggressive (not neutral, not CREATURE_FLAG_EXTRA_NO_AGGRO, not
 * tapped by someone else) and the segment comes within reach + MARGIN of it (2D). A creature
 * whose circle already holds A is left out: the bot stands there and nothing came.
 * Known limit: straight line only, wandering not included (the reach is the aggro range).
 * Since 2026-11-07 the brain asks Blocked: the navmesh route of MeleeRoutes once it is
 * checked, this straight line only before.
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

        // The walk of `bot` to `enemy` pulls another pack or finds no way: the navmesh route
        // (WorldModel::routes, Perception/Spatial/MeleeRoutes.h) when checked, else the straight line
        // above (not checked yet: the first refresh of a fight).
        // `tight`: on the hard line (RouteFact::safeTight, SafeRoute::TIGHT_MARGIN); the straight
        // line before the first check is the same either way.
        bool Blocked(WorldModel const& world, PartyMemberFacts const& bot, EnemyFacts const& enemy,
                     bool tight = false);

        // Yards `bot` walks to `enemy`: the route's path length when checked, else the straight
        // distance (centre to centre).
        float WalkYd(WorldModel const& world, PartyMemberFacts const& bot, EnemyFacts const& enemy);
    }
}

#endif
