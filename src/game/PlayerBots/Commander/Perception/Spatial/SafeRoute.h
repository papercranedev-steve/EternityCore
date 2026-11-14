/*
 * PartyBot Commander: "can a bot stand here / walk there without pulling another pack".
 *
 * Uses what the server knows: positions and aggro ranges of every hostile creature that is not
 * fighting yet (a player would know the room from looking at it). A point is unsafe when an idle
 * creature would attack the bot standing there (AggroRange.h: the server's proximity aggro rule,
 * line of sight included) with its range widened by the caller's margin, also from anywhere in
 * a random wanderer's circle (AggroRange::CoversWandering, since 2026-11-07: same rule as the
 * shot spots); a route is unsafe when the end point or any point sampled along the path every
 * PATH_SAMPLE_STEP is.
 *
 * Layer 1 (eyes): a fact about a point or a walk, no choice. Shared by the stance modules,
 * FleePoint (where to run from a melee enemy), FollowSpot (where a bot follows the leader),
 * ShotSpot, LosBreak and MeleeRoutes (the walks of melee bots to enemies). Moved here from
 * Behaviors/ on 2026-11-07. Map thread of the bot: reads creatures and pathfinds.
 */

#ifndef MANGOS_COMMANDER_SAFE_ROUTE_H
#define MANGOS_COMMANDER_SAFE_ROUTE_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;
class Map;
class PathInfo;
class Player;
class WorldObject;

namespace Commander
{
    namespace SafeRoute
    {
        // Idle creatures this far from the scan centre are looked at.
        float const SCAN_RADIUS = 70.0f;
        // Default margin: covers the bot's bounding radius and creatures turning to face it a moment later.
        float const IDLE_MARGIN = 5.0f;
        // The hard line (2026-11-14, stockade-20261114-203133): the bot's bounding radius and a
        // step, no comfort. IDLE_MARGIN is the preferred line; a member who cannot do its job
        // in the party's fight (out of reach or sight, no way in) may cross it, never this one.
        // Two cells facing each other across the Stockade corridor are 25-30 yd apart: with
        // IDLE_MARGIN the opposite cell's circle (aggro 21 + 5) closed the door of the cell the
        // tank fought in, and the three others stood in the corridor until the tank died; the
        // player walked through that door 23.3 yd from the creature and nothing came.
        float const TIGHT_MARGIN = 1.5f;
        // The path is checked against idle creatures every this many yards.
        float const PATH_SAMPLE_STEP = 3.0f;

        // Out-of-combat creatures that would attack `me`, with their reach (aggro range + margin).
        // Scanned once around `center` (default: `me`). Holds creature pointers: use it within the
        // same update, on the map thread.
        class IdleCreatures
        {
        public:
            explicit IdleCreatures(Player* me, WorldObject const* center = nullptr,
                                   float margin = IDLE_MARGIN, float scanRadius = SCAN_RADIUS);
            // Scanned around a point (a long walk: MeleeRoutes).
            IdleCreatures(Player* me, Position3 const& center, float scanRadius, float margin = IDLE_MARGIN);
            // A creature would attack the bot standing at `p`, or would once it wandered
            // (AggroRange::CoversWandering).
            bool Near(Position3 const& p) const { return Blocker(p) != nullptr; }
            // The first such creature, or null.
            Creature const* Blocker(Position3 const& p) const;
            bool Empty() const { return m_idle.empty(); }
            // Leave these out (they come anyway: the pull target and its group, for the walk
            // to a face pull spot).
            void Ignore(std::vector<ObjectGuid> const& guids);
            // For the walk out of an unsafe spot `from` (stance modules): a copy in which a
            // creature that already covers `from` only counts at points closer to its home than
            // `from` (by more than LEAVE_TOLERANCE). Without it every path sample inside the
            // circle failed and a bot standing deep in it found no way out
            // (strat-20261107-164236: the priest stopped 17.8 yd from a wanderer's home, reach
            // 22 + wander 5). Check the end point with the original (strict) set.
            IdleCreatures LeavingFrom(Position3 const& from) const;
            // For the walk to an enemy the party fights at `to` (MeleeRoutes): the same for a
            // creature that already covers `to`. The fight is there and it did not come: the
            // margin and its wander circle cover more than its real aggro range. Without it a
            // melee bot never joined a fight held 20-30 yd from other packs
            // (strat-20261108-232105: the rogue stood 9 yd from the tank for 70 s). Do not use
            // it for an enemy that is not fighting yet: its spot proves nothing.
            IdleCreatures ArrivingAt(Position3 const& to) const;

        private:
            IdleCreatures() = default;
            void Scan(Player* me, Map* map, Position3 const& center, float margin, float scanRadius);
            void CountOnlyCloserThan(Position3 const& end);
            struct Idle
            {
                Creature const* creature = nullptr;
                float reach = 0.0f;
                Position3 home;             // AggroRange::WanderOf (its position when not wandering)
                // >= 0: covers an end of the walk (LeavingFrom, ArrivingAt), the nearer one's
                // distance from its home: it counts only at points closer than that.
                float endGap = -1.0f;
            };
            std::vector<Idle> m_idle;
        };

        // A walk out or in may run along the circle (navmesh corners) at about the end's distance.
        float const END_GAP_TOLERANCE = 1.0f;

        // PathFinder still reports PATHFIND_NORMAL when `to` is up to 7 yd off the navmesh: the
        // path then ends at the mesh edge, e.g. in front of the wall `to` stands behind
        // (strat-20261102-232926: a patrol spot 4.6 yd behind a wall, the tank's backwards walk
        // stopped at the edge and the forward fallback walked into the wall).
        // A path ending this close to `to` reaches it.
        float const PATH_END_TOLERANCE = 1.5f;
        // Or a straight step of at most this many yards from the path's end, in line of sight (+2 yd).
        float const OFF_MESH_STEP = 3.0f;

        // The calculated navmesh path `path` gets to `to` (see PATH_END_TOLERANCE, OFF_MESH_STEP).
        bool PathReaches(Map const* map, PathInfo const& path, Position3 const& to);

        // The walk from `me` to `to` (pathfinding): reachable (PathReaches), at most `maxDetour`
        // times the straight line (+2 yd), and every sampled point outside `idle`. Without a
        // navmesh path (PATHFIND_NOT_USING_PATH): straight line of sight instead. `length`, if
        // given: the path's length (the straight distance without a navmesh path), set whenever
        // a path was found. `end`, if given: where the navmesh path ends (`to` without a navmesh
        // path), to tell a point on the mesh from one next to it (LosBreak: a crate top).
        // `verdict`, if given: why it is not safe (for the calculation trace and RouteFact).
        struct Verdict
        {
            char const* why = "";               // "" safe, "no path", "no sight", "does not reach", "detour", "idle creature"
            Creature const* blocker = nullptr;  // "idle creature": the first one a sample came into
        };
        bool PathIsSafe(Player* me, IdleCreatures const& idle, Position3 const& to, float maxDetour,
                        float* length = nullptr, Position3* end = nullptr, Verdict* verdict = nullptr);

        // The walk from `me` to `to` along the navmesh path (the straight line when there is no
        // complete path) is outside `idle`. No reachability or detour rule: for a walk that
        // happens anyway (following the leader), only which way is safe.
        bool WalkIsSafe(Player* me, IdleCreatures const& idle, Position3 const& to);

        // Every PATH_SAMPLE_STEP from `a` (excluded) to `b` (included) is outside `idle`.
        // `blocker`, if given: the creature of the first sample that is not.
        bool SegmentIsSafe(IdleCreatures const& idle, Position3 const& a, Position3 const& b,
                           Creature const** blocker = nullptr);

        float Distance2d(Position3 const& a, Position3 const& b);
    }
}

#endif
