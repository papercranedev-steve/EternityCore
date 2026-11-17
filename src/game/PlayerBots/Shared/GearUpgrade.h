/*
 * Shared bot facts (PlayerBots/Shared): is an item an upgrade for a bot? Used for need / greed
 * rolls (PartyBotAI) and for wearing what a bot looted (Inventory::WearUpgrades).
 *
 * Rough on purpose (first version, soul companions doc §4.2): the bot can use it, armor is of
 * the best armor type the bot wears (no cloth on a warrior), and its item level is above the
 * item in the slot it would go to (an empty slot always counts). Stats are not weighed yet.
 */

#ifndef MANGOS_BOTSHARED_GEAR_UPGRADE_H
#define MANGOS_BOTSHARED_GEAR_UPGRADE_H

#include "Common.h"

class Player;
struct ItemPrototype;

namespace BotShared
{
    namespace GearUpgrade
    {
        bool IsUpgrade(Player const* player, ItemPrototype const* proto);
    }
}

#endif
