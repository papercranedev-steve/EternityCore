#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Policies/SingletonImp.h"
#include "Item.h"
#include "AuctionHouseMgr.h"
#include "ObjectMgr.h"
#include "AuctionHouseBotMgr.h"
#include "Config/Config.h"
#include "Chat.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <limits>

INSTANTIATE_SINGLETON_1(AuctionHouseBotMgr);

AuctionHouseBotMgr::~AuctionHouseBotMgr()
{
    m_items.clear();

    if (m_config)
        m_config.reset();
}

void AuctionHouseBotMgr::Load()
{
    /* 1 - DELETE */
    m_items.clear();
    m_cumulativeWeights.clear();
    m_loaded = false;

    if (m_config)
        m_config.reset();

    /*2 - LOAD */
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `item`, `stack`, `stack_min`, `bid`, `buyout`, `weight` FROM `auctionhousebot`"));

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 AuctionHouseBot items");
        return;
    }

    uint32 count = 0;
    uint32 totalWeight = 0;
    BarGoLink bar(result->GetRowCount());

    Field* fields;
    do
    {
        bar.step();
        AuctionHouseBotEntry e;
        fields     = result->Fetch();
        e.item     = fields[0].GetUInt32();
        e.stack    = fields[1].GetUInt32();
        e.stackMin = fields[2].GetUInt32();
        e.bid      = fields[3].GetUInt32();
        e.buyout   = fields[4].GetUInt32();
        e.weight   = fields[5].GetUInt32();

        if (e.weight == 0)
            continue;

        if (e.weight > std::numeric_limits<uint32>::max() - totalWeight)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "AHBot::Load() : Total weight overflows at item %u, remaining rows ignored.", e.item);
            break;
        }

        totalWeight += e.weight;
        m_items.push_back(e);
        m_cumulativeWeights.push_back(totalWeight);

        ++count;
    }
    while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u AuctionHouseBot items", count);

    /* CONFIG */
    m_config                 = std::make_unique<AuctionHouseBotConfig>();
    m_config->enable         = sConfig.GetBoolDefault("AHBot.Enable", false);
    m_config->ahfid          = sConfig.GetIntDefault("AHBot.ah.fid", 120);
    m_config->itemcount      = sConfig.GetIntDefault("AHBot.itemcount", 2);
    m_config->priceVariance  = std::max(0, std::min(sConfig.GetIntDefault("AHBot.PriceVariance", 0), 90));

    m_auctionHouseEntry = sAuctionMgr.GetAuctionHouseEntry(m_config->ahfid);
    if (!m_auctionHouseEntry)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::Load() : No auction house for faction %u.", m_config->ahfid);
        return;
    }
    m_loaded = true;
}

void AuctionHouseBotMgr::Update(bool force /* = false */)
{
    if (!m_loaded)
        return;

    ASSERT(m_config);
    ASSERT(m_auctionHouseEntry);

    if (!(m_config->enable || force))
        return;

    if (m_items.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AHBot::Update() : Bad config or empty table.");
        return;
    }

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(m_auctionHouseEntry);
    if (!auctionHouse)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AHBot::Update() : No auction house for faction %u.", m_config->ahfid);
        return;
    }

    uint32 auctions = auctionHouse->GetCount();
    uint32 items    = m_config->itemcount;

    while (auctions < items)
    {
        AddItem(PickEntry(), auctionHouse);
        auctions++;
    }
}

AuctionHouseBotEntry const& AuctionHouseBotMgr::PickEntry() const
{
    uint32 roll = urand(0, m_cumulativeWeights.back() - 1);
    auto it = std::upper_bound(m_cumulativeWeights.begin(), m_cumulativeWeights.end(), roll);
    return m_items[it - m_cumulativeWeights.begin()];
}

void AuctionHouseBotMgr::AddItem(AuctionHouseBotEntry const& e, AuctionHouseObject *auctionHouse)
{
    ASSERT(m_auctionHouseEntry);

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(e.item);
    if (prototype == nullptr)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Item %u does not exist.", e.item);
        return;
    }

    Item* item = Item::CreateItem(e.item, 1);
    if (!item)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Cannot create item.");
        return;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "AHBot::AddItem() : Adding item %u.", e.item);

    uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(e.item);
    if (randomPropertyId != 0)
        item->SetItemRandomProperties(randomPropertyId);

    uint32 etime = urand(1, 3);
    switch (etime)
    {
        case 1:
            etime = 43200;
            break;
        case 2:
            etime = 86400;
            break;
        case 3:
            etime = 172800;
            break;
        default:
            etime = 86400;
            break;
    }
    // Table prices are for `stack` items: scale them to the stack actually listed,
    // then move them randomly so listings of the same item do not all cost the same.
    uint32 maxStack = std::max(1u, std::min(e.stack, prototype->GetMaxStackSize()));
    uint32 count = maxStack;
    if (e.stackMin > 0 && e.stackMin < maxStack)
        count = urand(e.stackMin, maxStack);

    double factor = double(count) / double(maxStack);
    if (m_config->priceVariance > 0)
        factor *= urand(100 - m_config->priceVariance, 100 + m_config->priceVariance) / 100.0;

    uint32 buyout = e.buyout ? std::max(1u, uint32(e.buyout * factor)) : 0;
    uint32 bid = std::max(1u, uint32(e.bid * factor));
    if (buyout && bid > buyout)
        bid = buyout;

    item->SetCount(count);

    uint32 dep = sAuctionMgr.GetAuctionDeposit(m_auctionHouseEntry, etime, item);

    AuctionEntry* auctionEntry       = new AuctionEntry;
    auctionEntry->Id                 = sObjectMgr.GenerateAuctionID();
    auctionEntry->auctionHouseEntry  = m_auctionHouseEntry;
    auctionEntry->itemGuidLow        = item->GetGUIDLow();
    auctionEntry->itemTemplate       = item->GetEntry();
    auctionEntry->owner              = 0;
    auctionEntry->startbid           = bid;
    auctionEntry->buyout             = buyout;
    auctionEntry->bidder             = 0;
    auctionEntry->bid                = 0;
    auctionEntry->deposit            = dep;
    auctionEntry->depositTime        = time(nullptr);
    auctionEntry->expireTime         = (time_t) etime + time(nullptr);

    item->SaveToDB();

    sAuctionMgr.AddAItem(item);
    auctionHouse->AddAuction(auctionEntry);
    auctionEntry->SaveToDB();
}

bool ChatHandler::HandleAHBotUpdateCommand(char *args)
{
    sAuctionHouseBotMgr.Update(true);
    SendSysMessage("[AHBot] Update finished.");
    return true;
}

bool ChatHandler::HandleAHBotReloadCommand(char *args)
{
    sAuctionHouseBotMgr.Load();
    SendSysMessage("[AHBot] Reload finished.");
    return true;
}
