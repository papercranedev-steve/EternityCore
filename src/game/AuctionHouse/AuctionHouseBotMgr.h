#ifndef _AUCTION_HOUSE_BOT_MGR_H
#define _AUCTION_HOUSE_BOT_MGR_H

#include "Policies/Singleton.h"
#include "SharedDefines.h"

#include <vector>
#include <memory>

class Player;

struct AuctionHouseBotEntry
{
    uint32 item;
    uint32 stack;       // max stack size; bid and buyout are prices for this many items
    uint32 stackMin;    // 0 = always list `stack` items, else random in [stackMin, stack]
    uint32 bid;
    uint32 buyout;
    uint32 weight;      // relative chance of being picked for a new listing
};

struct AuctionHouseBotConfig
{
    uint32 itemcount;
    uint32 ahfid;
    uint32 priceVariance; // percent, prices are moved randomly by up to +/- this much

    bool enable;
};

class AuctionHouseBotMgr
{
    public :
        AuctionHouseBotMgr() = default;
        ~AuctionHouseBotMgr();

        void Load();

        // force - put items in AH even if bot is disabled
        void Update(bool force = false);
        void AddItem(AuctionHouseBotEntry const& e, AuctionHouseObject *auctionHouse);

    protected:
        AuctionHouseBotEntry const& PickEntry() const;

        std::vector<AuctionHouseBotEntry> m_items;
        std::vector<uint32> m_cumulativeWeights; // running sum of m_items[i].weight
        std::unique_ptr<AuctionHouseBotConfig> m_config;
        AuctionHouseEntry const* m_auctionHouseEntry = nullptr;

        bool m_loaded = false;
};

#define sAuctionHouseBotMgr MaNGOS::Singleton< AuctionHouseBotMgr >::Instance()
#endif
