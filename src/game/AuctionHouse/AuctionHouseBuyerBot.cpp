#include "AuctionHouseBuyerBot.h"
#include "AuctionHouseBotMgr.h"
#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "Config/Config.h"
#include "DBCStores.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "TransactionLog.h"
#include "World.h"
#include "Policies/SingletonImp.h"

#include <algorithm>
#include <set>
#include <vector>

INSTANTIATE_SINGLETON_1(AuctionHouseBuyerBot);

namespace
{
    // splitmix64 finalizer: a stable pseudo-random number per auction id
    double RollFor(uint32 auctionId)
    {
        uint64 z = uint64(auctionId) + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        return double(z >> 11) / double(1ULL << 53);
    }

    // Every distinct auction house object (several DBC houses share one when linked / cross faction)
    std::vector<AuctionHouseObject*> AllAuctionHouses()
    {
        std::set<AuctionHouseObject*> seen;
        std::vector<AuctionHouseObject*> houses;
        for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
        {
            AuctionHouseEntry const* entry = sAuctionHouseStore.LookupEntry(i);
            if (!entry)
                continue;
            AuctionHouseObject* house = sAuctionMgr.GetAuctionsMap(entry);
            if (house && seen.insert(house).second)
                houses.push_back(house);
        }
        return houses;
    }
}

void AuctionHouseBuyerBot::Load()
{
    m_enable    = sConfig.GetBoolDefault("AHBuyer.Enable", false);
    m_sellDelay = uint32(std::max(0, sConfig.GetIntDefault("AHBuyer.SellDelay", 3600)));
    m_maxMarkup = std::max(1, sConfig.GetIntDefault("AHBuyer.MaxMarkup", 50)) / 100.0;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> AH buyer bot %s (delay %us, chance 0 at +%u%%)",
        m_enable ? "enabled" : "disabled", m_sellDelay, uint32(m_maxMarkup * 100.0 + 0.5));
}

AuctionBuyerDecision AuctionHouseBuyerBot::Evaluate(AuctionEntry const& auction) const
{
    AuctionBuyerDecision d;

    if (auction.owner == 0 || auction.bidder != 0)
        return d;

    Item const* item = sAuctionMgr.GetAItem(auction.itemGuidLow);
    if (!item)
        return d;

    if (!sAuctionHouseBotMgr.GetReferenceUnitPrice(auction.itemTemplate, d.referenceUnitPrice) || d.referenceUnitPrice <= 0.0)
        return d;

    d.price = auction.buyout ? auction.buyout : auction.startbid;
    if (!d.price)
        return d;

    d.eligible  = true;
    d.unitPrice = double(d.price) / double(std::max(1u, item->GetCount()));
    d.ratio     = d.unitPrice / d.referenceUnitPrice;

    if (d.ratio <= 1.0)
        d.chance = 1.0;
    else if (d.ratio >= 1.0 + m_maxMarkup)
        d.chance = 0.0;
    else
        d.chance = 1.0 - (d.ratio - 1.0) / m_maxMarkup;

    d.roll     = RollFor(auction.Id);
    d.willSell = d.roll < d.chance;

    time_t duration = std::max<time_t>(0, auction.expireTime - auction.depositTime);
    d.saleTime = auction.depositTime + std::min<time_t>(m_sellDelay, duration / 2);
    return d;
}

void AuctionHouseBuyerBot::Update()
{
    if (!m_enable)
        return;

    time_t now = sWorld.GetGameTime();

    for (AuctionHouseObject* house : AllAuctionHouses())
    {
        // collect first: Buy() removes entries from the map being walked
        std::vector<std::pair<AuctionEntry*, uint32>> due;
        for (auto const& itr : *house->GetAuctions())
        {
            AuctionBuyerDecision d = Evaluate(*itr.second);
            if (d.eligible && d.willSell && now >= d.saleTime)
                due.emplace_back(itr.second, d.price);
        }

        for (auto const& sale : due)
        {
            house->RemoveAuction(sale.first);
            Buy(sale.first, sale.second);
        }
    }
}

// Same steps as a player buyout (WorldSession::HandleAuctionPlaceBid), with nobody receiving the item.
// The auction must already be removed from its house.
void AuctionHouseBuyerBot::Buy(AuctionEntry* auction, uint32 price)
{
    Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);

    // bidder stays 0: there is no buyer character
    auction->bid = price;

    PlayerTransactionData data;
    data.type = "AHBuyer";
    data.parts[0].lowGuid = auction->owner;
    data.parts[0].itemsEntries[0] = auction->itemTemplate;
    data.parts[0].itemsCount[0] = item ? item->GetCount() : 0;
    data.parts[0].itemsGuid[0] = auction->itemGuidLow;
    data.parts[1].lowGuid = 0;
    data.parts[1].money = price;
    sWorld.LogTransaction(data);

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "AHBuyer: bought auction %u (item %u x%u) from %u for %u copper.",
        auction->Id, auction->itemTemplate, item ? item->GetCount() : 0, auction->owner, price);

    sAuctionMgr.SendAuctionSuccessfulMail(auction);

    if (item)
    {
        sAuctionMgr.RemoveAItem(item->GetGUIDLow());
        item->DeleteFromDB();
        delete item;
    }

    auction->DeleteFromDB();
    delete auction;
}

// .ahbot buyer: what the buyer bot will do with each player auction
bool ChatHandler::HandleAHBotBuyerCommand(char* /*args*/)
{
    PSendSysMessage("[AHBuyer] %s", sAuctionHouseBuyerBot.IsEnabled() ? "enabled" : "disabled (AHBuyer.Enable = 0)");

    time_t now = sWorld.GetGameTime();
    uint32 shown = 0, total = 0;
    for (AuctionHouseObject* house : AllAuctionHouses())
    {
        for (auto const& itr : *house->GetAuctions())
        {
            AuctionEntry const& auction = *itr.second;
            if (auction.owner == 0)
                continue;
            ++total;
            if (shown >= 30)
                continue;
            ++shown;

            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(auction.itemTemplate);
            char const* name = proto ? proto->Name1 : "?";
            AuctionBuyerDecision d = sAuctionHouseBuyerBot.Evaluate(auction);

            if (!d.eligible)
            {
                PSendSysMessage("#%u %s: not bought (%s)", auction.Id, name,
                    auction.bidder ? "has a player bid" : "no reference price");
                continue;
            }

            std::string outcome = "never sells";
            if (d.willSell)
            {
                int64 minutes = (int64(d.saleTime) - int64(now)) / 60;
                outcome = minutes > 0 ? "sells in " + std::to_string(minutes) + " min" : "sells next tick";
            }
            PSendSysMessage("#%u %s: unit %.0f / ref %.0f = %.0f%%, chance %.0f%%, roll %.0f%% -> %s",
                auction.Id, name, d.unitPrice, d.referenceUnitPrice, d.ratio * 100.0, d.chance * 100.0, d.roll * 100.0,
                outcome.c_str());
        }
    }
    PSendSysMessage("[AHBuyer] %u player auctions (%u shown).", total, shown);
    return true;
}
