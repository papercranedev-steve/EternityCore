/*
 * PartyBot Commander layer 1 (eyes): a boss room whose door shuts the moment the boss starts
 * its fight and stays shut until he is dead or the party wiped (the instance script; Baron
 * Rivendare's gate in Stratholme). Whoever is outside then cannot come in: closed doors block
 * movement, spells and, since 2026-11-28, swings (Unit::CanAutoAttackTarget).
 *
 * The door and where the fight starts come from the boss module (BossPrep::seal): a door shut by
 * a script is not in the map data, and the pull distance may be the boss's aura, not his aggro
 * range (Rivendare's Unholy Aura hits everyone within 30 yd every 2 s; the door is 28 yd from
 * him, so one step through it starts the fight).
 *
 * Geometry only: the room is the boss's side of a line through the door, square to the way
 * from the door to the boss. Rough for a door in a corner; good for a gate in a wall.
 */

#ifndef MANGOS_COMMANDER_SEALED_ROOM_H
#define MANGOS_COMMANDER_SEALED_ROOM_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"
#include <cmath>

namespace Commander
{
    struct SealedRoom
    {
        bool active = false;
        Position3 door;         // middle of the doorway
        Position3 boss;         // where the boss stands
        float pullYd = 0.0f;    // within this of the boss the fight starts

        // Everyone stands within this of the door, outside and out of pullYd, before the boss
        // is asked about and pulled: from there all cross within the first 2 s.
        static constexpr float GATHER_YD = 10.0f;
        // Out of pullYd by this much at least to count as safely outside.
        static constexpr float SAFE_MARGIN_YD = 1.0f;
        // The leader within this of the door, the boss not started: the bots follow close
        // behind him (HumanFollow), so they come through with him if he goes in.
        static constexpr float CLOSE_FOLLOW_YD = 30.0f;
        // Where the others walk to behind the tank: this far past the door.
        static constexpr float ENTER_YD = 6.0f;

        // How far `p` is past the door towards the boss; > 0 inside the room.
        float PastDoor(Position3 const& p) const
        {
            float ax = 0.0f, ay = 0.0f;
            if (!Axis(ax, ay))
                return 0.0f;
            return (p.x - door.x) * ax + (p.y - door.y) * ay;
        }
        bool Inside(Position3 const& p) const { return PastDoor(p) > 0.0f; }

        // Outside the room and out of pullYd of the boss (+ SAFE_MARGIN_YD): standing there starts nothing.
        bool SafeOutside(Position3 const& p) const
        {
            float const dx = p.x - boss.x;
            float const dy = p.y - boss.y;
            float const safe = pullYd + SAFE_MARGIN_YD;
            return !Inside(p) && dx * dx + dy * dy > safe * safe;
        }

        float DoorDistance(Position3 const& p) const
        {
            float const dx = p.x - door.x;
            float const dy = p.y - door.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // The point `yd` past the door on the way to the boss (the door's height).
        Position3 PointPast(float yd) const
        {
            Position3 out = door;
            float ax = 0.0f, ay = 0.0f;
            if (Axis(ax, ay))
            {
                out.x += ax * yd;
                out.y += ay * yd;
            }
            return out;
        }

        // Direction from the door to the boss, as an angle (atan2).
        float Bearing() const
        {
            return std::atan2(boss.y - door.y, boss.x - door.x);
        }

    private:
        bool Axis(float& ax, float& ay) const
        {
            float const dx = boss.x - door.x;
            float const dy = boss.y - door.y;
            float const len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.1f)
                return false;
            ax = dx / len;
            ay = dy / len;
            return true;
        }
    };
}

#endif
