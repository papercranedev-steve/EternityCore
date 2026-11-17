#include "Shared/Inventory.h"
#include "Shared/GearUpgrade.h"
#include "Player.h"
#include "Bag.h"
#include "Item.h"
#include "WorldSession.h"

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
}
}
