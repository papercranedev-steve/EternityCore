#include "Shared/Inventory.h"
#include "Shared/GearUpgrade.h"
#include "Player.h"
#include "Bag.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "WorldSession.h"
#include <algorithm>

namespace BotShared
{
namespace Inventory
{
    void CollectBagItemGuids(Player const* player, std::set<ObjectGuid>& guids)
    {
        guids.clear();
        for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                guids.insert(item->GetObjectGuid());

        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            if (Bag* bag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                    if (Item* item = bag->GetItemByPos(uint8(j)))
                        guids.insert(item->GetObjectGuid());
    }

    std::vector<Item*> BagItems(Player const* player)
    {
        std::vector<Item*> items;
        for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                items.push_back(item);

        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            if (Bag* bag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                    if (Item* item = bag->GetItemByPos(uint8(j)))
                        items.push_back(item);
        return items;
    }

    uint32 FreeBagSlots(Player const* player)
    {
        uint32 free = 0;
        for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                ++free;
        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            if (Bag* bag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                free += bag->GetFreeSlots();
        return free;
    }

    uint32 WearUpgrades(Player* player, std::set<ObjectGuid> const& before)
    {
        std::set<ObjectGuid> now;
        CollectBagItemGuids(player, now);
        uint32 worn = 0;
        for (ObjectGuid const& guid : now)
        {
            if (before.find(guid) != before.end())
                continue;
            Item* item = player->GetItemByGuid(guid);
            if (!item || item->IsEquipped() || !GearUpgrade::IsUpgrade(player, item->GetProto()))
                continue;
            WorldPackets::Item::AutoEquipItem packet;
            packet.srcbag = item->GetBagSlot();
            packet.srcslot = item->GetSlot();
            player->GetSession()->HandleAutoEquipItemOpcode(packet);
            if (item->IsEquipped())
                ++worn;
        }
        return worn;
    }

    namespace
    {
        bool IsPlainBag(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER;
        }

        // Where a plain bag of `size` slots goes: an empty bag slot, else the slot of the smallest
        // plain bag smaller than it. NULL_SLOT: nowhere.
        uint8 BagSlotFor(Player const* player, uint32 size)
        {
            uint8 best = NULL_SLOT;
            uint32 bestSize = size;
            for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            {
                Item const* bag = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!bag)
                    return slot;
                ItemPrototype const* proto = bag->GetProto();
                if (IsPlainBag(proto) && proto->ContainerSlots < bestSize)
                {
                    best = slot;
                    bestSize = proto->ContainerSlots;
                }
            }
            return best;
        }

        // An empty slot in the backpack or a plain bag other than the one in `bagSlot`.
        bool FreeSlotOutside(Player const* player, uint8 bagSlot, uint16& pos)
        {
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    pos = uint16(INVENTORY_SLOT_BAG_0) << 8 | i;
                    return true;
                }
            for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            {
                if (i == bagSlot)
                    continue;
                Bag const* bag = (Bag const*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
                if (!bag || !IsPlainBag(bag->GetProto()))
                    continue;
                for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                    if (!bag->GetItemByPos(uint8(j)))
                    {
                        pos = uint16(i) << 8 | j;
                        return true;
                    }
            }
            return false;
        }
    }

    uint32 WearBiggerBags(Player* player, std::set<ObjectGuid> const& before)
    {
        std::vector<Item*> bags;
        for (Item* item : BagItems(player))
            if (before.find(item->GetObjectGuid()) == before.end() && item->IsBag()
                && ((Bag*)item)->IsEmpty() && GearUpgrade::IsBagUpgrade(player, item->GetProto()))
                bags.push_back(item);
        // The biggest first, so it replaces the smallest.
        std::sort(bags.begin(), bags.end(), [](Item const* a, Item const* b)
        {
            return a->GetProto()->ContainerSlots > b->GetProto()->ContainerSlots;
        });

        uint32 worn = 0;
        for (Item* item : bags)
        {
            uint8 const slot = BagSlotFor(player, item->GetProto()->ContainerSlots);
            if (slot == NULL_SLOT)
                continue;
            // Not into the bag it sits in: take it out of there first.
            if (item->GetBagSlot() == slot)
            {
                uint16 free = 0;
                if (!FreeSlotOutside(player, slot, free))
                    continue;
                player->SwapItem(item->GetPos(), free);
                if (item->GetBagSlot() == slot)
                    continue;
            }
            // Into an empty slot, or over the old bag: its items move into the new one and the
            // old bag goes where the new one was (Player::SwapItem).
            uint16 const dest = uint16(INVENTORY_SLOT_BAG_0) << 8 | slot;
            player->SwapItem(item->GetPos(), dest);
            if (item->GetPos() == dest)   // IsEquipped() is false for bag slots
                ++worn;
        }
        return worn;
    }

    void WearPlainShirt(Player* player, uint32 pick)
    {
        // Sold by general goods vendors in every capital for 4 silver.
        static uint32 const plainShirts[] = { 16059 /*Brown*/, 16060 /*White*/, 3428 /*Gray*/ };

        if (player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BODY))
            return;

        uint32 const entry = plainShirts[pick % (sizeof(plainShirts) / sizeof(plainShirts[0]))];
        uint16 dest;
        if (player->CanEquipNewItem(EQUIPMENT_SLOT_BODY, dest, entry, false) == EQUIP_ERR_OK)
            player->EquipNewItem(dest, entry, true);
    }
}
}
