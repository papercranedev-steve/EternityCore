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
class WorldObject;

namespace Life
{
    namespace Errands
    {
        // A member of the bot's group (the bot too) is alive and in combat.
        bool GroupFights(Player* me);

        // The bot may go on an errand now: alive, out of combat, nobody of its group fighting,
        // not mounted, standing (not eating or drinking), the anchor (if any) not mounted
        // (the party is moving on). `seatedOk`: sitting (eating, drinking) does not stop it: an
        // errand still waiting for an answer while the bot follows (TownTrip's question).
        bool MayRun(Player* me, Player const* anchor, bool seatedOk = false);

        // No idle pack on the walk from the bot to x/y/z (Commander/Perception/Spatial/SafeRoute;
        // the scan covers the whole walk).
        bool WalkIsSafe(Player* me, float x, float y, float z, float maxDetour);

        // Objects an errand is done with (used, unreachable), the last `keep`.
        struct DoneMark
        {
            ObjectGuid guid;
            uint32 atMs = 0;
        };
        typedef std::deque<DoneMark> DoneList;

        // `forgetAfterMs`: a mark older than this no longer counts (0: until pushed out by
        // `keep` newer ones). For objects that stay (a quest giver): unreachable from where the
        // bot stood is not unreachable for good.
        bool IsDone(DoneList const& done, ObjectGuid guid, uint32 forgetAfterMs = 0);
        void MarkDone(DoneList& done, ObjectGuid guid, size_t keep);

        // The bot, already in reach of `target` (an object, a corpse), is close enough to use it
        // as a player would: a player walks up to what he clicks, reach alone would leave the bot
        // at its edge (5 yd past an object's model). Within `nearYd` of its centre (2D, plus a
        // little slack), or `walked` (the errand sent it towards the spot) and it stopped: the
        // spot is blocked (a wall, a slope), this is as close as it gets.
        bool CloseEnoughToUse(Player const* me, WorldObject const* target, float nearYd, bool walked);

        // Ends a walk the errand started (idle; the bot's follow starts again next tick).
        void StopWalk(Player* me);
    }
}

#endif
