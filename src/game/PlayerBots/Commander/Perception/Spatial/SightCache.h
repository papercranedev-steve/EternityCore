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
 * PathFailed / MarkPathFailed: a short memo of failed path checks (below), kept here since the
 * same modules use it on the same cells.
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
        Sight Cells(Unit const* viewer, Position3 const& from, Position3 const& to, uint32& budget);

        // The walk of `walker` to the cell of `to` failed SafeRoute::PathIsSafe (`tight`: on the
        // hard line) within PATH_FAIL_MS, from the cell it stands in now. The stance modules
        // check the path on a few cells per tick; without this the same failing cells came
        // first every tick (stockade-20261115-000917). Moving to another cell, or the time,
        // lets it be tried again (an idle creature walked off).
        static constexpr uint32 PATH_FAIL_MS = 3000;
        bool PathFailed(Unit const* walker, Position3 const& to, bool tight) const;
        void MarkPathFailed(Unit const* walker, Position3 const& to, bool tight);

    private:
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
