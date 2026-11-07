/*
 * PartyBot Commander layer 1 (eyes): can a melee or tank bot walk to an enemy without pulling
 * another pack, along the path it would really walk (WorldModel::routes).
 *
 * Before 2026-11-07 the brain only drew a straight line (RouteRisk::CrossesIdle): a walk the
 * navmesh takes around a pack was refused, and a straight line clear of packs whose real path
 * passes one was allowed. With JOIN_FIGHT_YD at 100 yd far melee bots more often stood still
 * (movement handoff doc, section 3).
 *
 * Pairs: every alive melee damage or tank bot with every enemy of WorldModel::enemies and
 * ::targeted within RANGE_YD, farther than REACH_YD from its edge (in reach nothing is walked).
 * A route is safe when SafeRoute::PathIsSafe passes (path found and reaching the enemy, at most
 * MAX_DETOUR times the straight line, every sample outside every idle creature's aggro range
 * + margin, wander circles included). The enemy and the creatures of its pack are left out:
 * they come anyway (an enemy the player just attacked is still idle). Idle creatures are
 * scanned once per bot around it, out to the farthest enemy checked + SCAN_EXTRA_YD.
 *
 * Cost: one navmesh query per pair. Results are kept until the bot or the enemy moved
 * MOVED_YD or MAX_AGE_MS passed (idle creatures wander); at most MAX_CHECKS_PER_REFRESH new
 * queries per refresh, unchecked pairs first, nearest first. A pair over the budget keeps its
 * old result (still reported); a pair never checked is missing from WorldModel::routes (the
 * brain falls back to the straight line, RouteRisk::Blocked).
 *
 * Map thread (PartyCommander::Refresh, after the packs): reads creatures and pathfinds.
 */

#ifndef MANGOS_COMMANDER_MELEE_ROUTES_H
#define MANGOS_COMMANDER_MELEE_ROUTES_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Map;

namespace Commander
{
    class MeleeRoutes
    {
    public:
        // Same as TargetRules.h JOIN_FIGHT_YD: no bot is sent farther.
        static constexpr float RANGE_YD = 100.0f;
        static constexpr float REACH_YD = 5.0f;         // DamageTargets / TankTargets MELEE_REACH
        static constexpr float MAX_DETOUR = 3.0f;       // a walk around a pack is still a walk
        static constexpr float MOVED_YD = 4.0f;
        static constexpr uint32 MAX_AGE_MS = 3000;
        static constexpr uint32 MAX_CHECKS_PER_REFRESH = 6;
        static constexpr float SCAN_EXTRA_YD = 30.0f;   // largest aggro range + margin, roughly
        static constexpr float MAX_SCAN_YD = 130.0f;

        // Fills world.routes. `map`: the map of the refreshing bot (pairs of bots on other maps
        // are not checked).
        void Annotate(Map* map, WorldModel& world, uint32 nowMs);

    private:
        struct Entry
        {
            RouteFact fact;
            Position3 from;
            Position3 to;
            uint32 atMs = 0;
        };
        std::vector<Entry> m_cache;
    };
}

#endif
