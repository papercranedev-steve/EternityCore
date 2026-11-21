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
#include <map>
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
            for (Item* item : Inventory::BagItems(player))
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
            VendorItem const* entry = nullptr;
            ItemPrototype const* proto = nullptr;
            uint32 price = 0;       // copper per vendor stack, discount included
        };

        // The vendor's best offer of the need's goods (Goods::Matches / Goods::Better).
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
                    if (!Goods::Matches(player, need.goods, proto))
                        continue;
                    if (best.proto && !Goods::Better(player, need.goods, proto, best.proto))
                        continue;
                    best.entry = v;
                    best.proto = proto;
                    best.price = uint32(proto->BuyPrice * discount + 0.5f);
                }
            }
            return best;
        }

    }

    bool HasJunk(Player const* player)
    {
        bool found = false;
        ForEachBagItem(player, [&found](Item* item) { found = found || IsJunk(item); });
        return found;
    }

    uint32 SellJunk(Player* player, Creature* vendor, uint32* stacksSold)
    {
        std::vector<ObjectGuid> junk;
        ForEachBagItem(player, [&junk](Item* item)
        {
            if (IsJunk(item))
                junk.push_back(item->GetObjectGuid());
        });
        if (stacksSold)
            *stacksSold = uint32(junk.size());
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

    uint32 RepairCost(Player const* player)
    {
        uint32 cost = 0;
        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                cost += RepairCostOf(item, 1.0f);
        ForEachBagItem(player, [&cost](Item* item) { cost += RepairCostOf(item, 1.0f); });
        return cost;
    }

    uint32 ListPrice(Player const* player, std::vector<Supplies::Need> const& needs)
    {
        uint32 total = 0;
        for (Supplies::Need const& need : needs)
        {
            uint32 const missing = need.Missing();
            if (!missing || need.recipe)
                continue;
            ItemPrototype const* best = nullptr;
            if (need.goods.kind == Goods::Kind::Item)
                best = sObjectMgr.GetItemPrototype(need.goods.id);
            else
                for (uint32 itemId : Supplies::SoldByVendors())
                {
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                    if (!proto || !Goods::Matches(player, need.goods, proto))
                        continue;
                    if (!best || Goods::Better(player, need.goods, proto, best))
                        best = proto;
                }
            total += UnitPrice(best) * missing;
        }
        return total;
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

    bool Sells(Player* player, Creature* vendor, Goods::Spec const& goods)
    {
        Supplies::Need need;
        need.goods = goods;
        return OfferFor(player, vendor, need).proto != nullptr;
    }

    uint32 UnitPrice(ItemPrototype const* proto)
    {
        return proto ? proto->BuyPrice / std::max<uint32>(proto->BuyCount, 1) : 0;
    }

    std::vector<Purchase> PlanPurchases(Player* player, Creature* vendor,
                                        std::vector<Supplies::Need> const& needs, Purchasing::Budget& budget)
    {
        std::vector<Purchase> plan;
        for (Supplies::Need const& need : needs)
        {
            uint32 const missing = budget.StillMissing(need);
            if (!missing)
                continue;
            Offer const offer = OfferFor(player, vendor, need);
            if (!offer.proto)
                continue;

            uint32 const perStack = std::max<uint32>(offer.proto->BuyCount, 1);
            uint32 const itemPlanned = budget.plannedOfItem[offer.proto->ItemId];
            uint32 stacks = (missing + perStack - 1) / perStack;
            if (offer.price)
                stacks = std::min(stacks, budget.MoneyFor(need.priority) / offer.price);
            if (offer.entry->maxcount)     // limited stock: the whole order fails beyond it
            {
                uint32 const left = vendor->GetVendorItemCurrentCount(offer.entry);
                uint32 const available = left > itemPlanned ? left - itemPlanned : 0;
                stacks = std::min(stacks, available / perStack);
            }
            stacks = std::min<uint32>(stacks, 255);     // one buy order at most (uint8)
            uint32 const slotsAllowed = budget.SlotsFor(need.priority);
            while (stacks && Purchasing::SlotsFor(player, offer.proto, stacks * perStack, itemPlanned) > slotsAllowed)
                --stacks;
            if (!stacks)
                continue;

            Purchase p;
            p.itemId = offer.proto->ItemId;
            p.stacks = stacks;
            p.cost = stacks * offer.price;
            plan.push_back(p);
            budget.Spend(need.goods, p.itemId, stacks * perStack, p.cost,
                         Purchasing::SlotsFor(player, offer.proto, stacks * perStack, itemPlanned));
        }
        return plan;
    }

    std::vector<Purchase> PlanPurchases(Player* player, Creature* vendor,
                                        std::vector<Supplies::Need> const& needs, uint32 reserve)
    {
        Purchasing::Budget budget = Purchasing::Budget::Of(player, reserve);
        return PlanPurchases(player, vendor, needs, budget);
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
