/*
 * PartyBot Commander layer 1 (eyes): the way a real player went (user, 2026-11-15). Bots that
 * cannot find a navmesh path to the leader, or only a long detour, walk it after him
 * (Behaviors/Movement/TrailFollow.h): the Gnomeregan drop to the lower level, the Blackrock
 * Depths window jump into the lava.
 *
 * WorldSession::HandleMovementOpcodes calls OnMove after a client movement packet was accepted
 * and applied (bots send none, so only real players are recorded), for players in a group
 * while the Commander is enabled. Per player, on the map it is on:
 *   Ground / Swim  every POINT_SPACING_YD of movement on the ground / in water;
 *   Takeoff        where it left the ground: the MSG_MOVE_JUMP packet's position, or the last
 *                  position on the ground before a packet with MOVEFLAG_FALLINGFAR (walked off);
 *   Land           MSG_MOVE_FALL_LAND, or the first packet no longer in the air (into water).
 * Nothing is recorded in the air. The trail starts again (previous points dropped) on another
 * map or instance, on a transport, and when one packet moves the player more than BREAK_YD on
 * the ground (teleport, Blink, summon: no way a bot can walk). Points older than KEEP_MS and
 * beyond MAX_POINTS are dropped.
 *
 * Facts only, no decisions. Any map thread writes, the bot's map thread reads: one lock.
 */

#ifndef MANGOS_COMMANDER_LEADER_TRAIL_H
#define MANGOS_COMMANDER_LEADER_TRAIL_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Player;

namespace Commander
{
    enum class TrailStep : uint8
    {
        Ground,
        Swim,
        Takeoff,        // the next point is the Land of this jump or fall
        Land,
    };

    struct TrailPoint
    {
        Position3 pos;
        uint32 timeMs = 0;
        TrailStep step = TrailStep::Ground;
        bool magma = false;     // the player stood in lava here (Player::IsInMagma)
    };

    namespace LeaderTrail
    {
        constexpr float POINT_SPACING_YD = 2.0f;
        // A run is about 3.5 yd per heartbeat (0.5 s), mounted about 7.
        constexpr float BREAK_YD = 15.0f;
        constexpr uint32 KEEP_MS = 120000;
        constexpr size_t MAX_POINTS = 600;

        // HandleMovementOpcodes hook (the mover's map thread), after HandleMoverRelocation.
        void OnMove(Player const* mover, uint16 opcode);

        // The trail of `player` on this map and instance, oldest first; empty if none.
        std::vector<TrailPoint> Copy(ObjectGuid player, uint32 mapId, uint32 instanceId);
    }
}

#endif
