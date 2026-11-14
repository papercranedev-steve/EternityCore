/*
 * PartyBot Commander layer 1 (eyes): builds the position field (PositionField.h, doc
 * .kiro/docs/2026-11-10-position-field.md) into WorldModel::field on every perception refresh.
 *
 * Steps of a build:
 *  1. Blocks: every BLOCK_YD block within COVER_YD of a participant, at most MAX_BLOCKS
 *     (nearest to a participant first). Built while the party has an enemy, a pull runs or a
 *     member is in combat (an ally's fight: WorldModel::InFight), and while travelling when an
 *     idle creature or a patrol's walk could reach us (ThreatNear); otherwise no field.
 *  2. Ground: flood from the participants' cells. A cell not known yet gets its ground near the
 *     height of the cell it was reached from (navmesh nearest polygon within
 *     SEARCH_HALF_HEIGHT), a pair of neighbours not known yet gets a navmesh raycast. Ground and
 *     links never change and are kept per block (GROUND_KEEP_MS after the last use), so a fight
 *     pays for its area once. At most MAX_NAV_QUERIES per build: the flood continues over what
 *     is known and the rest comes in the next builds. A participant standing more than
 *     SEARCH_HALF_HEIGHT off its cell's kept height (another floor) gets its cell found again.
 *  3. Idle layer: idle creatures within reach of the field (AggroRange::IsIdleAggressor
 *     against any member; reach = the farthest over the members + SafeRoute::IDLE_MARGIN),
 *     nearest MAX_IDLE. Coverage of a creature standing still or wandering is kept per cell
 *     while its home stays within IDLE_MOVED_YD and its reach the same (and the cell's ground
 *     was not found again on another floor); at most MAX_SIGHT_CHECKS line of sight checks per
 *     build on cells not decided yet, cells over the budget count as covered by distance and
 *     height (safe side) and are checked in the next builds. (Before 2026-11-14 it was kept
 *     per block and only for blocks with every cell's ground known: blocks touching a wall
 *     never were, every build checked them again and hit the budget.)
 *  5. The sight cache (SightCache.h) is kept across builds and set on every field.
 *  4. Patrol layer: PatrolFacts::track of aggressive patrols (one lap), seconds from this
 *     build (the tracks are from the last pack refresh).
 *
 * Map thread of the party (PartyCommander::Refresh, after the facts and the version step):
 * reads creatures and the navmesh of that thread.
 */

#ifndef MANGOS_COMMANDER_POSITION_FIELD_BUILDER_H
#define MANGOS_COMMANDER_POSITION_FIELD_BUILDER_H

#include "Commander/Perception/Spatial/PositionField.h"
#include <memory>
#include <unordered_map>
#include <vector>

class Map;

namespace Commander
{
    struct TeamPlan;
    class SightCache;

    class PositionFieldBuilder
    {
    public:
        static constexpr uint32 MAX_BLOCKS = 160;
        static constexpr uint32 MAX_NAV_QUERIES = 3000;
        static constexpr uint32 MAX_SIGHT_CHECKS = 600;
        // Ground of a neighbour is looked for this far above and below the cell it is reached from.
        static constexpr float SEARCH_HALF_HEIGHT = 3.0f;
        // Largest height step between straight neighbours that is still walked (about 50 degrees).
        static constexpr float MAX_STEP_YD = 2.4f;
        // Navmesh raycast end and the neighbour's ground agree within this.
        static constexpr float LINK_HEIGHT_TOLERANCE = 1.0f;
        static constexpr uint32 GROUND_KEEP_MS = 10 * 60 * 1000;
        static constexpr size_t MAX_KEPT_BLOCKS = 4000;
        static constexpr float IDLE_MOVED_YD = 1.0f;

        // `plan`: the plan of the previous refresh (the pull's planned fight spot), read before
        // the assigners of this refresh run.
        void Build(Map* map, WorldModel& world, TeamPlan const& plan, uint32 nowMs);

    private:
        enum : uint8 { GROUND_UNKNOWN = 0, GROUND_YES = 1, GROUND_NO = 2 };
        struct GroundCell
        {
            float z = 0.0f;
            uint8 state = GROUND_UNKNOWN;
            uint8 links = 0;
            uint8 linksKnown = 0;       // bit FieldDir: links bit is decided
        };
        struct GroundBlock
        {
            GroundCell cells[PositionField::BLOCK_CELLS * PositionField::BLOCK_CELLS];
            uint32 usedMs = 0;
        };
        // Bit per cell of a block: `checked` = decided (by distance, or by the line of sight
        // check), `bits` = covered (only meaningful where checked).
        struct CoveredBlock
        {
            uint64 bits = 0;
            uint64 checked = 0;
        };
        struct IdleCover
        {
            ObjectGuid guid;
            Position3 home;
            float reach = 0.0f;
            uint32 usedMs = 0;
            // Block key -> its cells.
            std::unordered_map<uint64, CoveredBlock> covered;
        };

        GroundCell* Ground(int32 cx, int32 cy, bool create);
        void FloodGround(Map* map, PositionField& field, std::vector<Position3> const& seeds, uint32 nowMs);
        void BuildIdle(Map* map, WorldModel const& world, PositionField& field, Position3 const& center,
                       float radius, uint32 nowMs);
        void BuildPatrols(WorldModel const& world, PositionField& field);
        // Travelling: an aggressive idle creature or a patrol's walk comes within COVER_YD of a
        // member's reach (pack and patrol facts; the field is not built otherwise).
        static bool ThreatNear(WorldModel const& world);
        void Forget(uint32 nowMs);

        std::unordered_map<uint64, GroundBlock> m_ground;
        std::vector<IdleCover> m_idleCovers;
        std::vector<uint64> m_groundChanged;   // blocks with a cell found again on another floor (FloodGround)
        std::shared_ptr<SightCache> m_sight;   // kept across builds, handed to every field
        std::shared_ptr<PositionField> m_field;
        uint32 m_maxBuildMicros = 0;
    };
}

#endif
