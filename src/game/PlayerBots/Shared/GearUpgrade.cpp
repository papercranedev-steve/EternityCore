#include "Shared/GearUpgrade.h"
#include "Player.h"
#include "Item.h"
#include "ItemPrototype.h"
#include <algorithm>

namespace BotShared
{
namespace GearUpgrade
{
    namespace
    {
        bool IsBodyArmor(uint32 inventoryType)
        {
            switch (inventoryType)
            {
                case INVTYPE_HEAD:
                case INVTYPE_SHOULDERS:
                case INVTYPE_CHEST:
                case INVTYPE_ROBE:
                case INVTYPE_WAIST:
                case INVTYPE_LEGS:
                case INVTYPE_FEET:
                case INVTYPE_WRISTS:
                case INVTYPE_HANDS:
                    return true;
                default:
                    return false;   // cloak, neck, rings, trinkets, shields, off-hand items
            }
        }

        // The heaviest armor the player has learned to wear.
        uint32 BestArmor(Player const* player)
        {
            if (player->HasSkill(SKILL_PLATE_MAIL))
                return ITEM_SUBCLASS_ARMOR_PLATE;
            if (player->HasSkill(SKILL_MAIL))
                return ITEM_SUBCLASS_ARMOR_MAIL;
            if (player->HasSkill(SKILL_LEATHER))
                return ITEM_SUBCLASS_ARMOR_LEATHER;
            return ITEM_SUBCLASS_ARMOR_CLOTH;
        }

        uint32 ItemLevelAt(Player const* player, uint8 slot)
        {
            Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            return item ? item->GetProto()->ItemLevel : 0;
        }
    }

    bool IsUpgrade(Player const* player, ItemPrototype const* proto)
    {
        if (!player || !proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
            return false;
        if (player->CanUseItem(proto) != EQUIP_ERR_OK)
            return false;
        if (proto->Class == ITEM_CLASS_ARMOR && IsBodyArmor(proto->InventoryType) && proto->SubClass != BestArmor(player))
            return false;

        // Two slots: measured against the weaker one.
        if (proto->InventoryType == INVTYPE_FINGER)
            return proto->ItemLevel > std::min(ItemLevelAt(player, EQUIPMENT_SLOT_FINGER1), ItemLevelAt(player, EQUIPMENT_SLOT_FINGER2));
        if (proto->InventoryType == INVTYPE_TRINKET)
            return proto->ItemLevel > std::min(ItemLevelAt(player, EQUIPMENT_SLOT_TRINKET1), ItemLevelAt(player, EQUIPMENT_SLOT_TRINKET2));

        uint8 const slot = player->FindEquipSlot(proto, NULL_SLOT, true);
        if (slot == NULL_SLOT)
            return false;
        return proto->ItemLevel > ItemLevelAt(player, slot);
    }
}
}
