/*
 * Shared bot facts (PlayerBots/Shared): how far a walk really is, along the navmesh (Detour's
 * A* through the bot's filter: player bots pay BOT_SWIM_COST per yard of open water, so the
 * path goes around it when it can; closed doors cut it, PathFinder.cpp).
 */

#ifndef MANGOS_BOTSHARED_WALK_LENGTH_H
#define MANGOS_BOTSHARED_WALK_LENGTH_H

#include "Common.h"

class Player;

namespace BotShared
{
    namespace WalkLength
    {
        struct Point
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Yards from `from` to `to` for `walker`; < 0 when there is no full path (no mesh path,
        // only part of the way, ends elsewhere). No navmesh here: the straight distance.
        float Between(Player const* walker, Point const& from, Point const& to);
    }
}

#endif
