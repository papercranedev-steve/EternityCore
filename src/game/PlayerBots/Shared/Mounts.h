/*
 * Shared bot core: riding. The apprentice mounts of each race (the ones its mount vendor sells
 * at level 40), as spells: a bot casting one rides as a player on that mount does.
 */

#ifndef MANGOS_BOTSHARED_MOUNTS_H
#define MANGOS_BOTSHARED_MOUNTS_H

#include "Common.h"

class Player;

namespace BotShared
{
namespace Mounts
{
    // Level the first mount is ridden at (Apprentice Riding).
    constexpr uint32 FIRST_MOUNT_LEVEL = 40;

    // One of the race's apprentice mounts, picked by `seed` (the same seed, the same mount).
    // 0 for a race without one.
    uint32 RacialMountSpell(uint8 race, uint32 seed);

    // Rides `spell` now. `instant`: no cast time (out of sight at login), else cast as a
    // player does (3 s, interrupted by moving). False: it could not (in combat, shapeshifted,
    // indoors, ...).
    bool Mount(Player* me, uint32 spell, bool instant);
}
}

#endif
