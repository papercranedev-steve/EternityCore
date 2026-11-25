/*
 * PartyBot Commander layer 1 (eyes): line of sight from a position field cell to a target,
 * kept for the fight and shared by the stance modules (2026-11-14,
 * stockade-20261114-224312: the mage's 48 sight checks per tick went to the same nearest cells
 * behind the corridor wall; the cells that saw into the prisoners' cell were never reached).
 *
 * Heights as Spell::CheckCast (WorldObject::IsWithinLOSInMap): from the viewer's collision
 * height above `from`, to TARGET_HEIGHT above the target's feet (GetLosCheckPosition). The
 * stance modules checked eye to eye at 2 yd before; the spell's line is lower and could
 * disagree at a doorway.
 *
 * Cells(viewer, from, to, budget): from the centre of `from`'s field cell (at from.z) to the
 * centre of `to`'s cell (at to.z). The answer is kept per pair of cells; it is checked again
 * when either height differs by more than Z_TOLERANCE (another floor), the viewer's height
 * step differs, or it is older than KEEP_MS (a door opened). A check not kept costs one of
 * `budget`; with none left the answer is Unknown (the caller decides which side that is).
 * Live(viewer, from, to): the same line now, exact positions, nothing kept: the final check
 * of the spot a module takes.
 *
 * LiveFromCell(viewer, cell, to): Live from the ground the viewer will stand on at a cell.
 *
 * PathFailed / MarkPathFailed: a short memo of failed path checks (below), kept here since the
 * same modules use it on the same cells. StandFailed / MarkStandFailed: the same for a cell the
 * bot stood on and did not see the target from.
 *
 * Owned by PositionFieldBuilder (kept across builds, dropped with the field), reached through
 * PositionField::sight. Map thread of the party.
 */

#ifndef MANGOS_COMMANDER_SIGHT_CACHE_H
#define MANGOS_COMMANDER_SIGHT_CACHE_H

#include "Commander/Perception/WorldModel.h"
#include <unordered_map>
#include <vector>

class Unit;

namespace Commander
{
    class SightCache
    {
    public:
        static constexpr float TARGET_HEIGHT = 1.0f;
        static constexpr float Z_TOLERANCE = 1.0f;
        static constexpr float EYE_STEP = 0.25f;
        static constexpr uint32 KEEP_MS = 30 * 1000;
        static constexpr size_t MAX_ENTRIES = 60000;

        enum class Sight : uint8 { Unknown, Yes, No };

        static bool Live(Unit const* viewer, Position3 const& from, Position3 const& to);
        // Live from where the viewer will stand on a field cell: the cell's z is the navmesh's,
        // the bot stands on the ground (Map::GetHeight, within STAND_Z_TOLERANCE of the cell).
        // On a grazing line the two disagreed (stockade-20261125-221620: the hunters on the
        // ramp below the platform walked between two cells for 8 s, each seen from the other).
        static constexpr float STAND_Z_TOLERANCE = 1.0f;
        static bool LiveFromCell(Unit const* viewer, Position3 const& cell, Position3 const& to);
        Sight Cells(Unit const* viewer, Position3 const& from, Position3 const& to, uint32& budget);

        // The walk of `walker` to the cell of `to` failed SafeRoute::PathIsSafe (`tight`: on the
        // hard line) within PATH_FAIL_MS, from the cell it stands in now. The stance modules
        // check the path on a few cells per tick; without this the same failing cells came
        // first every tick (stockade-20261115-000917). Moving to another cell, or the time,
        // lets it be tried again (an idle creature walked off).
        static constexpr uint32 PATH_FAIL_MS = 3000;
        bool PathFailed(Unit const* walker, Position3 const& to, bool tight) const;
        void MarkPathFailed(Unit const* walker, Position3 const& to, bool tight);

        // `viewer` stood on the cell of `spot` and its Live line to the cell of `target` failed,
        // within STAND_FAIL_MS. The search checks a cell from afar; standing there is the
        // final answer, so the cell is not taken again for that target (the same loop as
        // above, whatever made the two checks disagree). The target moving to another cell,
        // or the time, lets it be tried again.
        static constexpr uint32 STAND_FAIL_MS = 10000;
        bool StandFailed(Unit const* viewer, Position3 const& spot, Position3 const& target) const;
        // From where `viewer` stands now.
        void MarkStandFailed(Unit const* viewer, Position3 const& target);

    private:
        struct StandFail
        {
            uint64 viewer = 0;      // ObjectGuid raw
            int32 atX = 0, atY = 0, targetX = 0, targetY = 0;
            uint32 atMs = 0;
        };
        std::vector<StandFail> m_standFails;

        struct PathFail
        {
            uint64 walker = 0;      // ObjectGuid raw
            int32 fromX = 0, fromY = 0, toX = 0, toY = 0;
            bool tight = false;
            uint32 atMs = 0;
        };
        std::vector<PathFail> m_pathFails;

        struct Entry
        {
            float fromZ = 0.0f;
            float toZ = 0.0f;
            uint32 atMs = 0;
            uint8 eye = 0;
            bool sees = false;
        };
        std::unordered_map<uint64, Entry> m_entries;
        uint32 m_mapId = 0;
    };
}

#endif
