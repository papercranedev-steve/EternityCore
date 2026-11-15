/*
 * PartyBot Commander layer 1 (eyes): the way a real player came into a dungeon (user 2026-11-15,
 * contingencies: a party that cannot win runs back out through the door; inside a dungeon
 * creatures never leash, Creature::IsOutOfThreatArea, so the door is the only way out of a
 * chase). LeaderTrail keeps the last two minutes for following; this keeps the whole way since
 * the zone-in, made short:
 *
 *   points   every POINT_SPACING_YD on the ground or in water, oldest (the zone-in) first.
 *   loops    a new point within LOOP_YD (and LOOP_DZ height) of an older one cuts everything
 *            after that one: walking back down a corridor, or a round through a room, leaves
 *            the shortest known way back. Rough: two points this close behind a thin wall
 *            would be joined too.
 *   drops    a jump or fall that came down more than CLIMB_YD: the link from the point before
 *            is one way (WayPoint::oneWay), the way back cannot go up there.
 *   gaps     one packet moved the player more than BREAK_YD on the ground but at most
 *            TELEPORT_YD (Blink, a knockback): the link is not walked (WayPoint::gap). Farther
 *            (a teleport, a summon), on a transport, on another map or instance: starts again.
 *
 * Only on dungeon maps (Map::IsDungeon), for players in a group while the Commander is
 * enabled. Way::fromEntrance: the first point lies within ENTRANCE_YD of the map's zone-in
 * point (ObjectMgr::GetMapEntranceTrigger), so the way starts at the door.
 *
 * Facts only, no decisions. WorldSession::HandleMovementOpcodes calls OnMove (the mover's map
 * thread); Copy may run on any thread: one lock. Back is pure.
 */

#ifndef MANGOS_COMMANDER_WAY_IN_H
#define MANGOS_COMMANDER_WAY_IN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Player;

namespace Commander
{
    struct WayPoint
    {
        Position3 pos;
        bool oneWay = false;    // came down a drop from the point before: no way back up
        bool gap = false;       // a Blink or knockback from the point before: not walked
    };

    struct Way
    {
        std::vector<WayPoint> points;   // oldest (the door end) first
        bool fromEntrance = false;
        float startToEntranceYd = -1.0f; // first point to the zone-in point, -1 = no zone-in known
        uint32 startedMs = 0;
    };

    // The walk from a position back along a Way to its first point (Back).
    struct WayBack
    {
        bool found = false;     // a point within JOIN_YD (and JOIN_DZ) of the position
        size_t joinIndex = 0;
        float joinYd = 0.0f;    // straight, the position to the joined point
        float lengthYd = 0.0f;  // joinYd + the links from there to the first point
        uint32 oneWays = 0;     // drops on the way back: it cannot be walked
        uint32 gaps = 0;        // links nobody walked
        // The door can be walked to along it: found, from the entrance, no drop.
        bool ToDoor(Way const& way) const { return found && way.fromEntrance && oneWays == 0; }
    };

    namespace WayIn
    {
        constexpr float POINT_SPACING_YD = 5.0f;
        constexpr float LOOP_YD = 4.0f;
        constexpr float LOOP_DZ = 3.0f;
        constexpr float CLIMB_YD = 3.0f;
        constexpr float BREAK_YD = 15.0f;       // = LeaderTrail::BREAK_YD
        constexpr float TELEPORT_YD = 50.0f;
        constexpr float ENTRANCE_YD = 30.0f;
        constexpr size_t MAX_POINTS = 4000;     // 20 km at the spacing; beyond, the oldest go (not from the door any more)
        constexpr uint32 KEEP_MS = 30 * 60 * 1000;  // no packet for this long: the record is dropped
        // Back: the position joins the nearest point within these.
        constexpr float JOIN_YD = 30.0f;
        constexpr float JOIN_DZ = 5.0f;

        // HandleMovementOpcodes hook (the mover's map thread), after HandleMoverRelocation.
        void OnMove(Player const* mover, uint16 opcode);

        // The way of `player` into this map and instance; empty points if none.
        Way Copy(ObjectGuid player, uint32 mapId, uint32 instanceId);

        // The walk from `from` back to the way's first point. Sight to the joined point is the
        // caller's check (pure: no map).
        WayBack Back(Way const& way, Position3 const& from);

        // Sum of the links from point `index` back to the first point.
        float LengthBack(Way const& way, size_t index);
    }
}

#endif
