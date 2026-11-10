/*
 * PartyBot Commander layer 1 (eyes): builds the position field (PositionField.h, doc
 * .kiro/docs/2026-11-10-position-field.md) into WorldModel::field on every perception refresh.
 *
 * Steps of a build:
 *  1. Blocks: every BLOCK_YD block within COVER_YD of a participant, at most MAX_BLOCKS
 *     (nearest to a participant first). No participant fighting and no pull: no field.
 *  2. Ground: flood from the participants' cells. A cell not known yet gets its ground near the
 *     height of the cell it was reached from (navmesh nearest polygon within
 *     SEARCH_HALF_HEIGHT), a pair of neighbours not known yet gets a navmesh raycast. Ground and
 *     links never change and are kept per block (GROUND_KEEP_MS after the last use), so a fight
 *     pays for its area once. At most MAX_NAV_QUERIES per build: the flood continues over what
 *     is known and the rest comes in the next builds. A participant standing more than
 *     SEARCH_HALF_HEIGHT off its cell's kept height (another floor) gets its cell found again.
 *  3. Idle layer: idle creatures within reach of the field (AggroRange::IsIdleAggressor
 *     against any member; reach = the farthest over the members + SafeRoute::IDLE_MARGIN),
 *     nearest MAX_IDLE. Coverage of a creature standing still or wandering is kept per block
 *     while its home stays within IDLE_MOVED_YD and its reach the same; at most
 *     MAX_SIGHT_CHECKS line of sight checks per build, cells over the budget count as covered
 *     by distance and height (safe side) and are checked again next build.
 *  4. Patrol layer: PatrolFacts::track of aggressive patrols (filled while the party fights).
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
        struct IdleCover
        {
            ObjectGuid guid;
            Position3 home;
            float reach = 0.0f;
            uint32 usedMs = 0;
            // Block key -> bit per cell of that block (checked with line of sight).
            std::unordered_map<uint64, uint64> covered;
        };

        GroundCell* Ground(int32 cx, int32 cy, bool create);
        void FloodGround(Map* map, PositionField& field, std::vector<Position3> const& seeds, uint32 nowMs);
        void BuildIdle(Map* map, WorldModel const& world, PositionField& field, Position3 const& center,
                       float radius, uint32 nowMs);
        void BuildPatrols(WorldModel const& world, PositionField& field);
        void Forget(uint32 nowMs);

        std::unordered_map<uint64, GroundBlock> m_ground;
        std::vector<IdleCover> m_idleCovers;
        std::shared_ptr<PositionField> m_field;
        uint32 m_maxBuildMicros = 0;
    };
}

#endif
