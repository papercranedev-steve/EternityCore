#include "Shared/Vendor.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Creature.h"
#include "Bag.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "DBCStores.h"
#include "WorldSession.h"
#include <algorithm>
#include <vector>

namespace BotShared
{
namespace Vendor
{
    namespace
    {
        template <class Fn>
        void ForEachBagItem(Player const* player, Fn fn)
        {
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    fn(item);
            for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
                if (Bag* bag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                        if (Item* item = bag->GetItemByPos(uint8(j)))
                            fn(item);
        }

        bool IsJunk(Item const* item)
        {
            ItemPrototype const* proto = item->GetProto();
            return proto && proto->Quality == ITEM_QUALITY_POOR && proto->SellPrice > 0 &&
                   proto->Class != ITEM_CLASS_QUEST && !proto->StartQuest;
        }

        // Player::DurabilityRepair's price of one item (it computes it but returns 0).
        uint32 RepairCostOf(Item const* item, float discount)
        {
            uint32 const maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
            uint32 const curDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
            if (!maxDurability || curDurability >= maxDurability)
                return 0;
            ItemPrototype const* proto = item->GetProto();
            DurabilityCostsEntry const* dcost = sDurabilityCostsStore.LookupEntry(proto->ItemLevel);
            DurabilityQualityEntry const* dquality = sDurabilityQualityStore.LookupEntry((proto->Quality + 1) * 2);
            if (!dcost || !dquality)
                return 0;
            uint32 const multiplier = dcost->multiplier[ItemSubClassToDurabilityMultiplierId(proto->Class, proto->SubClass)];
            uint32 costs = uint32((maxDurability - curDurability) * multiplier * dquality->quality_mod);
            costs = uint32(costs * discount + 0.5f);
            return std::max<uint32>(costs, 1);
        }

        struct Offer
        {
            ItemPrototype const* proto = nullptr;
            uint32 price = 0;       // copper per vendor stack, discount included
        };

        // The vendor's offer for a need: its exact item, or its best usable ammo of the subclass.
        Offer OfferFor(Player* player, Creature* vendor, Supplies::Need const& need)
        {
            Offer best;
            float const discount = player->GetReputationPriceDiscount(vendor);
            for (VendorItemData const* list : { vendor->GetVendorItems(), vendor->GetVendorTemplateItems() })
            {
                if (!list)
                    continue;
                for (VendorItem const* v : list->m_items)
                {
                    if (!v)
                        continue;
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(v->item);
                    if (!proto || !player->IsVendorItemVisible(vendor, v, proto))
                        continue;
                    if (need.itemId)
                    {
                        if (proto->ItemId != need.itemId)
                            continue;
                    }
                    else if (proto->Class != ITEM_CLASS_PROJECTILE || proto->SubClass != need.ammoSubClass ||
                             proto->RequiredLevel > player->GetLevel() || player->CanUseAmmo(proto->ItemId) != EQUIP_ERR_OK ||
                             (best.proto && best.proto->ItemLevel >= proto->ItemLevel))
                        continue;
                    best.proto = proto;
                    best.price = uint32(proto->BuyPrice * discount + 0.5f);
                    if (need.itemId)
                        return best;
                }
            }
            return best;
        }

        // New bag slots `units` more of the item take (the last partial stack is filled first).
        uint32 SlotsFor(Player const* player, ItemPrototype const* proto, uint32 units)
        {
            uint32 const stack = std::max<uint32>(proto->GetMaxStackSize(), 1);
            uint32 const have = player->GetItemCount(proto->ItemId);
            uint32 const room = have % stack ? stack - have % stack : 0;
            uint32 const extra = units > room ? units - room : 0;
            return (extra + stack - 1) / stack;
        }
    }

    bool HasJunk(Player const* player)
    {
        bool found = false;
        ForEachBagItem(player, [&found](Item* item) { found = found || IsJunk(item); });
        return found;
    }

    uint32 SellJunk(Player* player, Creature* vendor)
    {
        std::vector<ObjectGuid> junk;
        ForEachBagItem(player, [&junk](Item* item)
        {
            if (IsJunk(item))
                junk.push_back(item->GetObjectGuid());
        });
        uint32 const before = player->GetMoney();
        for (ObjectGuid const& guid : junk)
        {
            WorldPackets::Item::SellItem packet;
            packet.vendorGuid = vendor->GetObjectGuid();
            packet.itemGuid = guid;
            packet.count = 0;   // the whole stack
            player->GetSession()->HandleSellItemOpcode(packet);
        }
        return player->GetMoney() > before ? player->GetMoney() - before : 0;
    }

    uint32 RepairCost(Player* player, Creature const* npc)
    {
        float const discount = player->GetReputationPriceDiscount(npc);
        uint32 cost = 0;
        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                cost += RepairCostOf(item, discount);
        ForEachBagItem(player, [&cost, discount](Item* item) { cost += RepairCostOf(item, discount); });
        return cost;
    }

    uint32 RepairAll(Player* player, Creature* npc)
    {
        uint32 const before = player->GetMoney();
        WorldPackets::Npc::RepairItem packet;
        packet.npcGuid = npc->GetObjectGuid();
        packet.itemGuid = ObjectGuid();     // empty: everything
        player->GetSession()->HandleRepairItemOpcode(packet);
        return before > player->GetMoney() ? before - player->GetMoney() : 0;
    }

    std::vector<Purchase> PlanPurchases(Player* player, Creature* vendor,
                                        std::vector<Supplies::Need> const& needs, uint32 reserve)
    {
        std::vector<Purchase> plan;
        uint32 money = player->GetMoney();
        uint32 freeSlots = Inventory::FreeBagSlots(player);

        for (Supplies::Need const& need : needs)
        {
            Offer const offer = OfferFor(player, vendor, need);
            if (!offer.proto)
                continue;

            bool const critical = need.priority == Supplies::Priority::Critical;
            uint32 const budget = critical ? money : (money > reserve ? money - reserve : 0);
            uint32 const slotsAllowed = critical ? freeSlots : (freeSlots > KEEP_FREE_SLOTS ? freeSlots - KEEP_FREE_SLOTS : 0);

            uint32 const perStack = std::max<uint32>(offer.proto->BuyCount, 1);
            uint32 stacks = (need.Missing() + perStack - 1) / perStack;
            if (offer.price)
                stacks = std::min(stacks, budget / offer.price);
            stacks = std::min<uint32>(stacks, 255);     // one buy order at most (uint8)
            while (stacks && SlotsFor(player, offer.proto, stacks * perStack) > slotsAllowed)
                --stacks;
            if (!stacks)
                continue;

            Purchase p;
            p.itemId = offer.proto->ItemId;
            p.stacks = stacks;
            p.cost = stacks * offer.price;
            plan.push_back(p);
            money -= p.cost;
            freeSlots -= SlotsFor(player, offer.proto, stacks * perStack);
        }
        return plan;
    }

    uint32 Buy(Player* player, Creature* vendor, std::vector<Purchase> const& purchases)
    {
        uint32 const before = player->GetMoney();
        for (Purchase const& p : purchases)
            player->BuyItemFromVendor(vendor->GetObjectGuid(), p.itemId, uint8(p.stacks), NULL_BAG, NULL_SLOT);
        return before > player->GetMoney() ? before - player->GetMoney() : 0;
    }
}
}
