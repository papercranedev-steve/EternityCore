/*
 * PartyBot Commander: "can a bot stand here / walk there without pulling another pack".
 *
 * Uses what the server knows: positions and aggro ranges of every hostile creature that is not
 * fighting yet (a player would know the room from looking at it). A point is unsafe inside
 * max(MIN_IDLE_CLEARANCE, aggro range + IDLE_MARGIN) of such a creature; a route is unsafe when
 * the end point or any point sampled along the path every PATH_SAMPLE_STEP is.
 *
 * Shared by FleePoint (where to run from a melee enemy) and HealerStanceBehavior (where a healer
 * stands in combat). Map thread of the bot: reads creatures and pathfinds.
 */

#ifndef MANGOS_COMMANDER_SAFE_ROUTE_H
#define MANGOS_COMMANDER_SAFE_ROUTE_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Player;

namespace Commander
{
    namespace SafeRoute
    {
        // Idle creatures this far from the bot are looked at.
        float const SCAN_RADIUS = 70.0f;
        // The margin covers the bot's bounding radius and creatures turning to face it a moment later.
        float const IDLE_MARGIN = 5.0f;
        float const MIN_IDLE_CLEARANCE = 10.0f;
        // Only creatures within this height difference count (floors above / below do not aggro).
        float const IDLE_MAX_HEIGHT_DIFF = 10.0f;
        // The path is checked against idle creatures every this many yards.
        float const PATH_SAMPLE_STEP = 3.0f;

        // Out-of-combat hostile creatures around a bot, with their clearance. Scanned once.
        class IdleCreatures
        {
        public:
            explicit IdleCreatures(Player* me, float scanRadius = SCAN_RADIUS);
            bool Near(Position3 const& p) const;
            bool Empty() const { return m_idle.empty(); }

        private:
            struct Idle
            {
                Position3 pos;
                float clearance = 0.0f;
            };
            std::vector<Idle> m_idle;
        };

        // The walk from `me` to `to` (pathfinding): reachable, at most `maxDetour` times the
        // straight line (+2 yd), and every sampled point outside `idle`. Without a navmesh path
        // (PATHFIND_NOT_USING_PATH): straight line of sight instead.
        bool PathIsSafe(Player* me, IdleCreatures const& idle, Position3 const& to, float maxDetour);

        float Distance2d(Position3 const& a, Position3 const& b);
    }
}

#endif
