#include "Shared/AuctionMarket.h"
#include "Shared/Vendor.h"
#include "AuctionHouseMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include <algorithm>

namespace BotShared
{
namespace AuctionMarket
{
    std::vector<Listing> Find(Player* player, Creature* auctioneer, Goods::Spec const& goods)
    {
        std::vector<Listing> out;
        AuctionHouseEntry const* houseEntry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer);
        AuctionHouseObject* house = houseEntry ? sAuctionMgr.GetAuctionsMap(houseEntry) : nullptr;
        if (!house)
            return out;

        for (auto const& itr : *house->GetAuctions())
        {
            AuctionEntry* e = itr.second;
            if (!e || !e->buyout || e->owner == player->GetGUIDLow() || !e->IsAvailableFor(player))
                continue;
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(e->itemTemplate);
            if (!proto || !Goods::Matches(player, goods, proto))
                continue;
            Item const* item = sAuctionMgr.GetAItem(e->itemGuidLow);
            uint32 const count = item ? std::max<uint32>(item->GetCount(), 1) : 1;
            // No dearer than a vendor's price for the same item.
            if (Supplies::IsSoldByVendors(proto->ItemId))
            {
                uint32 const vendorUnit = Vendor::UnitPrice(proto);
                if (vendorUnit && e->buyout > uint64(vendorUnit) * count)
                    continue;
            }
            Listing l;
            l.auctionId = e->Id;
            l.itemId = proto->ItemId;
            l.count = count;
            l.buyout = e->buyout;
            out.push_back(l);
        }
        std::sort(out.begin(), out.end(), [](Listing const& a, Listing const& b)
        {
            // a.buyout / a.count < b.buyout / b.count
            return uint64(a.buyout) * b.count < uint64(b.buyout) * a.count;
        });
        return out;
    }

    std::vector<Listing> PlanBuys(Player* player, Creature* auctioneer, std::vector<Supplies::Need> const& needs,
                                  Purchasing::Budget& budget, std::set<uint32>& taken)
    {
        std::vector<Listing> plan;
        for (Supplies::Need const& need : needs)
        {
            uint32 missing = budget.StillMissing(need);
            if (!missing)
                continue;
            for (Listing const& l : Find(player, auctioneer, need.goods))
            {
                if (!missing)
                    break;
                if (taken.count(l.auctionId))
                    continue;
                if (l.count > missing + missing * MAX_OVERSHOOT_PCT / 100)
                    continue;
                if (l.buyout > budget.MoneyFor(need.priority))
                    continue;
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(l.itemId);
                uint32 const slots = Purchasing::SlotsFor(player, proto, l.count, budget.plannedOfItem[l.itemId]);
                if (slots > budget.SlotsFor(need.priority))
                    continue;
                taken.insert(l.auctionId);
                plan.push_back(l);
                budget.Spend(need.goods, l.itemId, l.count, l.buyout, slots);
                missing = l.count >= missing ? 0 : missing - l.count;
            }
        }
        return plan;
    }

    uint32 Buyout(Player* player, Creature* auctioneer, std::vector<Listing> const& listings)
    {
        uint32 asked = 0;
        for (Listing const& l : listings)
        {
            auto packet = std::make_unique<WorldPackets::AuctionHouse::AuctionPlaceBid>();
            packet->auctioneerGuid = auctioneer->GetObjectGuid();
            packet->auctionId = l.auctionId;
            packet->price = l.buyout;
            player->GetSession()->QueuePacket(std::move(packet));
            asked += l.buyout;
        }
        return asked;
    }
}
}
