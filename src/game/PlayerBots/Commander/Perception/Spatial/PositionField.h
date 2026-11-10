/*
 * PartyBot Commander layer 1 (eyes): the position field, a grid over the fight area with the
 * spatial facts every spot choice needs (doc .kiro/docs/2026-11-10-position-field.md).
 *
 * Built by PositionFieldBuilder on every perception refresh while the party fights or a pull
 * runs, and while travelling near creatures that could attack us (WorldModel::field, null
 * otherwise). Pure data once built: modules only read it.
 *
 * Area. The world is cut into fixed BLOCK_YD squares aligned to world coordinates, each holding
 * BLOCK_CELLS x BLOCK_CELLS cells of CELL_YD. The field holds every block within COVER_YD of a
 * participant (living party members, enemies fighting the party, the planned fight spot and the
 * pack's position while a pull runs), so a long fight line is covered along its length and two
 * groups far apart each get their own patch. COVER_YD = the longest heal / cast range (28-30)
 * and the largest Range hazard zone (36): no member stands near the edge.
 *
 * Cells. Cell (cx, cy) covers [cx * CELL_YD, (cx + 1) * CELL_YD) and the same in y; its centre
 * is the point a module walks to.
 *  - z: the walkable ground at the centre, found from the neighbour it was reached from (so a
 *    long slope rises cell by cell; a floor under a bridge is a different layer and is not
 *    reached). Navmesh (nearest walkable polygon), or Map::GetHeight on maps without one.
 *  - links: bit d = a straight walk to the neighbour in FieldDir d stays on the navmesh surface
 *    (navmesh raycast: thin walls and rails cut it). Without a navmesh: height step only.
 *  - standable: on the ground and linked to a participant's cell through other cells.
 *  - idle: bit i = idle()[i] would attack a member standing here (AggroRange: aggro range of the
 *    member it reaches farthest + SafeRoute::IDLE_MARGIN, wander circle, height, line of sight;
 *    moving creatures without a wander circle by distance and height only).
 *  - patrolSec: earliest second (from the build) a patrol's zone (aggro range +
 *    PATROL_MARGIN) covers the cell, from PatrolFacts::track (one lap of its path);
 *    NO_PATROL if its walk never comes there.
 * Hazard zones are not stored: HazardZones::IsInside is plain math over a few zones.
 *
 * Walking distance (WalkYd): shortest walk over linked standable cells from a member's cell
 * (straight 2 yd, diagonal 2.8 yd, height difference included). Cells an idle creature covers
 * are not entered, except the circles the member already stands in, and those only moving away
 * from the creature's home (same rule as SafeRoute::IdleCreatures::LeavingFrom). Computed once
 * per member and build, on first use. A grid walk is an estimate: the spot finally chosen is
 * still checked with SafeRoute::PathIsSafe.
 *
 * Map thread of the party (like the rest of WorldModel); the walk cache is filled on reads.
 */

#ifndef MANGOS_COMMANDER_POSITION_FIELD_H
#define MANGOS_COMMANDER_POSITION_FIELD_H

#include "Commander/Perception/WorldModel.h"
#include <unordered_map>
#include <vector>

namespace Commander
{
    // The 8 neighbours, counter-clockwise from east.
    enum FieldDir : uint8
    {
        FIELD_E, FIELD_NE, FIELD_N, FIELD_NW, FIELD_W, FIELD_SW, FIELD_S, FIELD_SE,
        FIELD_DIRS
    };

    struct FieldCell
    {
        static constexpr float NO_PATROL = -1.0f;

        float z = 0.0f;
        bool standable = false;
        uint8 links = 0;                // bit FieldDir
        uint64 idle = 0;                // bit index into PositionField::Idle()
        float patrolSec = NO_PATROL;
    };

    // An idle creature of the idle layer.
    struct FieldIdle
    {
        ObjectGuid guid;
        Position3 home;                 // AggroRange::WanderOf (its position when not wandering)
        float reach = 0.0f;             // aggro range + margin, farthest over the members
        float wander = 0.0f;            // wander radius, 0 = none
        bool sightChecked = false;      // false: distance and height only (moving, or over budget)
    };

    struct FieldStats
    {
        uint32 blocks = 0;
        uint32 standable = 0;
        uint32 idleCells = 0;           // standable cells some idle creature covers
        uint32 patrolCells = 0;         // standable cells a patrol reaches within its track
        uint32 newCells = 0;            // ground found this build
        uint32 navQueries = 0;          // nearest polygon + raycast calls this build
        uint32 sightChecks = 0;         // line of sight checks of the idle layer this build
        uint32 buildMicros = 0;
        uint32 maxBuildMicros = 0;      // since the server started
        bool navmesh = false;
        bool budgetHit = false;         // some ground or sight was left for the next build
    };

    class PositionField
    {
    public:
        static constexpr float CELL_YD = 2.0f;
        static constexpr int32 BLOCK_CELLS = 8;
        static constexpr float BLOCK_YD = CELL_YD * BLOCK_CELLS;
        static constexpr float COVER_YD = 36.0f;
        static constexpr uint32 MAX_IDLE = 64;          // bits of FieldCell::idle
        static constexpr float PATROL_MARGIN = 5.0f;    // PatrolAssigner::ZONE_MARGIN

        static int32 CellOf(float v);
        static int32 BlockOf(int32 cell);
        static void Step(uint8 dir, int32& dx, int32& dy);
        static float StepYd(uint8 dir) { return (dir & 1) ? CELL_YD * 1.41421356f : CELL_YD; }

        bool Empty() const { return m_blocks.empty(); }
        // Null outside the field.
        FieldCell const* At(int32 cx, int32 cy) const;
        FieldCell const* At(Position3 const& p) const { return At(CellOf(p.x), CellOf(p.y)); }
        // Centre of the cell, at its ground height (the given z if outside the field).
        Position3 CenterOf(int32 cx, int32 cy) const;
        std::vector<FieldIdle> const& Idle() const { return m_idle; }

        // Shortest grid walk (yards) from `from` to the cell of `to`, -1 = not reachable over the
        // field (outside it, not standable, blocked by idle creatures).
        float WalkYd(PartyMemberFacts const& from, Position3 const& to) const;

        // Every cell of the field: fn(cx, cy, cell).
        template <class Fn>
        void ForEachCell(Fn fn) const
        {
            for (Block const& b : m_blocks)
                for (int32 i = 0; i < BLOCK_CELLS * BLOCK_CELLS; ++i)
                    fn(b.bx * BLOCK_CELLS + i % BLOCK_CELLS, b.by * BLOCK_CELLS + i / BLOCK_CELLS, b.cells[i]);
        }

        uint32 worldVersion = 0;        // WorldModel::version it was built for
        FieldStats stats;

    private:
        friend class PositionFieldBuilder;

        struct Block
        {
            int32 bx = 0;
            int32 by = 0;
            FieldCell cells[BLOCK_CELLS * BLOCK_CELLS];
        };
        static uint64 Key(int32 a, int32 b) { return (uint64(uint32(a)) << 32) | uint32(b); }
        // Index into the flat cell numbering (block index * cells per block + cell), -1 outside.
        int32 FlatIndex(int32 cx, int32 cy) const;
        FieldCell* MutableAt(int32 cx, int32 cy);
        void Clear();

        std::vector<Block> m_blocks;
        std::unordered_map<uint64, uint32> m_blockIndex;   // Key(bx, by) -> m_blocks index
        std::vector<FieldIdle> m_idle;

        struct Walk
        {
            ObjectGuid from;
            std::vector<float> yd;      // per flat index, < 0 = not reached
        };
        mutable std::vector<Walk> m_walks;
        Walk const& WalkFrom(PartyMemberFacts const& from) const;
    };
}

#endif
