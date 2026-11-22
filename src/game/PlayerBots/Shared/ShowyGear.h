/*
 * Shared bot facts (PlayerBots/Shared): what of a player's look catches the eye (population bots
 * admiring a player, user 2026-11-22). Judged per visible piece against the player's own level,
 * not against the bots' gear (that gap vanishes at 60):
 *
 *   level 1-39:  a blue piece of item level >= level + 5, any epic;
 *   level 40-59: an epic, a blue piece of item level >= level + 8, riding a mount;
 *   level 60:    only epics, tier set pieces, legendaries and an epic mount.
 *
 * Points: legendary 4, epic set piece 3, epic (or epic mount) 2, blue (or a mount below 60) 1.
 * Visible pieces: head, shoulders, chest, back, both hands, ranged.
 *
 * Any thread that may read the player (map thread).
 */

#ifndef MANGOS_BOTSHARED_SHOWY_GEAR_H
#define MANGOS_BOTSHARED_SHOWY_GEAR_H

#include "Common.h"
#include <vector>

class Player;

namespace BotShared
{
    namespace ShowyGear
    {
        enum class Look : uint8 { Weapon, Head, Shoulders, Chest, Back, Mount };

        struct Thing
        {
            uint32 key = 0;         // item entry; a mount: its spell id | MOUNT_KEY
            uint8 points = 0;
            Look look = Look::Weapon;
        };

        constexpr uint32 MOUNT_KEY = 0x80000000u;
        constexpr uint32 BLUE_ABOVE_LOW = 5;        // levels 1-39
        constexpr uint32 BLUE_ABOVE_MID = 8;        // levels 40-59
        constexpr uint32 MOUNT_FROM_LEVEL = 40;

        // Eye-catching things on `p` now (empty: nothing to look at).
        std::vector<Thing> Of(Player const* p);
    }
}

#endif
