/*
 * Shared bot hands (PlayerBots/Shared): bag snapshots and putting on new upgrades.
 */

#ifndef MANGOS_BOTSHARED_INVENTORY_H
#define MANGOS_BOTSHARED_INVENTORY_H

#include "Common.h"
#include "ObjectGuid.h"
#include <set>
#include <vector>

class Item;
class Player;

namespace BotShared
{
    namespace Inventory
    {
        // Every item in the backpack and the bags (not equipped, not the bank).
        void CollectBagItemGuids(Player const* player, std::set<ObjectGuid>& guids);
        std::vector<Item*> BagItems(Player const* player);

        // Empty slots in the backpack and the bags (special bags such as quivers included).
        uint32 FreeBagSlots(Player const* player);

        // Puts on what came into the bags since `before` and is an upgrade (GearUpgrade.h),
        // through the equip handler: level, class and proficiency checks apply and the old item
        // goes to the bags. Returns how many were put on.
        uint32 WearUpgrades(Player* player, std::set<ObjectGuid> const& before);
    }
}

#endif
