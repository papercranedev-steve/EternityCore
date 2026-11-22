/*
 * Shared bot core (PlayerBots/Shared): names from game data in the reader's client language, for
 * our own lines ("{target} is on me"). Creatures: locales_creature by the reader's session
 * locale (Creature::GetNameForLocaleIdx), else the template's name. Players and pets: their name.
 */

#ifndef MANGOS_BOTSHARED_GAME_NAMES_H
#define MANGOS_BOTSHARED_GAME_NAMES_H

#include "Common.h"
#include <string>

class Player;
class Unit;

namespace BotShared
{
namespace GameNames
{
    // `reader` null: the default name.
    std::string UnitName(Unit const* unit, Player const* reader);
}
}

#endif
