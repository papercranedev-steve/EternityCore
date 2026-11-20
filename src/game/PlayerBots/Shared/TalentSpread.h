/*
 * Shared bot facts (PlayerBots/Shared): how many talent points a character has in each of its
 * class's three talent trees (tab page 0-2, the order the client's talent frame shows them).
 * Works for an online player and for an offline character (its learned spell ids).
 */

#ifndef MANGOS_BOTSHARED_TALENT_SPREAD_H
#define MANGOS_BOTSHARED_TALENT_SPREAD_H

#include "Common.h"
#include <array>
#include <unordered_set>

class Player;

namespace BotShared
{
    namespace TalentSpread
    {
        using Points = std::array<uint32, 3>;

        Points Of(Player const* player);
        // classId: CLASS_*; spells: every learned spell id (character_spell).
        Points Of(uint8 classId, std::unordered_set<uint32> const& spells);
    }
}

#endif
