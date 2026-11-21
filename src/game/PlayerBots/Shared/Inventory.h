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

        // Puts on the plain bags that came in since `before` and are bigger (GearUpgrade::
        // IsBagUpgrade): into an empty bag slot, else over the smallest plain bag, whose items
        // move into the new one; the old bag stays in the bags. Returns how many were put on.
        uint32 WearBiggerBags(Player* player, std::set<ObjectGuid> const& before);

        // Every bot wears a shirt, and only the cheap vendor ones (Common Brown / White / Gray
        // Shirt): gear is precious in this world, a plain shirt even commoners own.
        // Does nothing when the shirt slot is taken. `pick` chooses the colour (any number).
        void WearPlainShirt(Player* player, uint32 pick);
    }
}

#endif
