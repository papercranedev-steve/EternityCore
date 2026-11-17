/*
 * Bot daily life (PlayerBots/Life): what every errand checks the same way.
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_ERRAND_COMMON_H
#define MANGOS_LIFE_ERRAND_COMMON_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>

class Player;

namespace Life
{
    namespace Errands
    {
        // A member of the bot's group (the bot too) is alive and in combat.
        bool GroupFights(Player* me);

        // The bot may go on an errand now: alive, out of combat, nobody of its group fighting,
        // not mounted, standing (not eating or drinking), the anchor (if any) not mounted
        // (the party is moving on).
        bool MayRun(Player* me, Player const* anchor);

        // No idle pack on the walk from the bot to x/y/z (Commander/Perception/Spatial/SafeRoute;
        // the scan covers the whole walk).
        bool WalkIsSafe(Player* me, float x, float y, float z, float maxDetour);

        // Objects an errand is done with (used, unreachable), the last `keep`.
        bool IsDone(std::deque<ObjectGuid> const& done, ObjectGuid guid);
        void MarkDone(std::deque<ObjectGuid>& done, ObjectGuid guid, size_t keep);

        // Ends a walk the errand started (idle; the bot's follow starts again next tick).
        void StopWalk(Player* me);
    }
}

#endif
