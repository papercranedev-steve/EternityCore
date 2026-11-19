/*
 * Bot daily life (PlayerBots/Life): walking up to one place in town, the leg every trip is made
 * of. Used by the companions' town trips (Life/Errands/TownTrip.h: NPC stops) and by the
 * population bots' day in town (Population/TownLife.h: spots at the town's anchors). No leader,
 * no party: only the bot and its destination.
 *
 * A destination is an object (a creature or a gameobject: reached in interaction reach) or a
 * point (reached within reachYd). Walking repaths every REPATH_MS (the target may move a
 * little, a path may be cut short). Arrived: the bot stops and faces the object (a point: its
 * face point, if it has one). Failed: the object is gone (dead, in combat) or the walk took
 * longer than the give-up time.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_APPROACH_H
#define MANGOS_LIFE_TRIP_APPROACH_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;
class WorldObject;

namespace Life
{
namespace Trip
{
    struct Destination
    {
        ObjectGuid target;          // a creature or a gameobject; empty: the point below
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float reachYd = 1.5f;       // point: arrived this close
        bool hasFace = false;       // point: look at (faceX, faceY) on arrival
        float faceX = 0.0f, faceY = 0.0f;
        bool run = true;            // false: walk (a few steps in town)
    };

    struct ApproachState
    {
        uint32 sinceMs = 0;         // on the way since
        uint32 nextMoveMs = 0;
        uint32 arrivedMs = 0;       // 0: not there yet
    };

    enum class ApproachResult : uint8
    {
        Walking,
        Arrived,
        Failed,
    };

    namespace Approach
    {
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 60 * IN_MILLISECONDS;

        // A new leg starts now.
        void Begin(ApproachState& state, uint32 now);
        // The object of a destination, nullptr: gone (a creature also when dead or fighting).
        WorldObject* TargetOf(Player* me, ObjectGuid const& guid);
        ApproachResult Update(Player* me, Destination const& dest, ApproachState& state, uint32 now,
                              uint32 giveUpMs = GIVE_UP_MS);
    }
}
}

#endif
