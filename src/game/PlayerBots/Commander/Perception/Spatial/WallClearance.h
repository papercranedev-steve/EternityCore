/*
 * PartyBot Commander layer 1 (eyes): how far a place is from the nearest wall, and a short
 * step that takes it clear of one.
 *
 * A bot's movement is the server's: it walks the navmesh, nothing stops its body at a wall. The
 * mesh keeps only the build's agent radius (0.2 yd on continents, 0.3 yd in dungeons,
 * contrib/mmap TileWorker) between its edge and the wall, less than a character's half width:
 * a bot that stops at the mesh edge (a path round a corner, a follow point or a hiding point
 * inside the wall) shows half its body inside the wall. A player cannot get there, his client
 * collides with the wall model.
 *
 * Distances are to the navmesh edge (a wall, a ledge, a fence: whatever bounds the ground).
 * Facts only, no decisions. Map thread (the navmesh query is per thread).
 */

#ifndef MANGOS_COMMANDER_WALL_CLEARANCE_H
#define MANGOS_COMMANDER_WALL_CLEARANCE_H

#include "Common.h"

class Player;

namespace Commander
{
    namespace WallClearance
    {
        // Wanted room between a character (standing or seated) and the mesh edge, about 1 yd
        // to the wall.
        constexpr float BODY_ROOM_YD = 0.8f;
        // Steps tried around a place, shortest first; no step longer than the last.
        constexpr float STEP_YD[] = { 0.5f, 1.0f, 1.5f };
        constexpr uint32 STEP_DIRS = 8;

        struct Spot
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Distance from `at` (snapped to the ground mesh within a yard) to the nearest mesh
        // edge, up to `maxYd`. < 0: unknown (no navmesh, not near the mesh).
        float RoomAt(uint32 mapId, Spot const& at, float maxYd);

        // A spot at most STEP_YD's last from `from`, along the mesh (no step through a wall),
        // with `wantYd` room: the shortest step that has it, else the step with the most room.
        // False: `from` has the room already, no step gains any, or unknown (RoomAt < 0).
        bool StepClear(uint32 mapId, Spot const& from, float wantYd, Spot& out);

        // The same for where a bot stands (unknown on a transport or in water).
        bool StepClear(Player const* me, float wantYd, Spot& out);
    }
}

#endif
