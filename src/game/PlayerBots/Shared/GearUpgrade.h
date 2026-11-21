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
        // Item levels gained over what it replaces; 0 = not an upgrade (same rules as IsUpgrade).
        uint32 UpgradeGain(Player const* player, ItemPrototype const* proto);

        // A plain bag (not a quiver, soul or herb bag) it would wear: a bag slot is empty, or it
        // has more slots than the smallest plain bag it wears (Inventory::WearBiggerBags swaps it in).
        bool IsBagUpgrade(Player const* player, ItemPrototype const* proto);
    }
}

#endif
