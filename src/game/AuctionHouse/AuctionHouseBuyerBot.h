#ifndef _AUCTION_HOUSE_BUYER_BOT_H
#define _AUCTION_HOUSE_BUYER_BOT_H

/*
 * AH buyer bot: buys players' auctions so a single-player server still has a market.
 *
 * Reference price: the AH bot table price per item (AuctionHouseBotMgr::GetReferenceUnitPrice,
 * i.e. `auctionhousebot`.buyout / stack). Items not in that table are never bought.
 *
 * For each player auction without a player bid (owner != 0, bidder == 0):
 *   price   = buyout, or start bid when there is no buyout
 *   ratio   = (price / item count) / reference unit price
 *   chance  = 1                               if ratio <= 1
 *             1 - (ratio - 1) / MaxMarkup     if 1 < ratio < 1 + MaxMarkup
 *             0                               otherwise
 *   roll    = hash(auction id) in [0, 1)      same result after a restart, no state is stored
 *   sells   = roll < chance
 *   sale at = posting time + min(SellDelay, auction duration / 2)
 *
 * A sale pays the seller through the normal "auction successful" mail (price + deposit - AH cut)
 * and destroys the item. Unsold auctions expire and go back to the seller as usual.
 * Runs on the world auction timer (once a minute), so sales land up to a minute late.
 *
 * Config (mangosd.conf):
 *   AHBuyer.Enable      0/1
 *   AHBuyer.SellDelay   seconds after posting, default 3600
 *   AHBuyer.MaxMarkup   percent above the reference price at which the chance reaches 0, default 50
 */

#include "Policies/Singleton.h"
#include "SharedDefines.h"

#include <ctime>

struct AuctionEntry;

struct AuctionBuyerDecision
{
    bool   eligible = false;  // player auction, no bid, item has a reference price
    uint32 price = 0;         // copper the bot would pay for the whole auction
    double unitPrice = 0.0;
    double referenceUnitPrice = 0.0;
    double ratio = 0.0;
    double chance = 0.0;
    double roll = 0.0;
    time_t saleTime = 0;
    bool   willSell = false;
};

class AuctionHouseBuyerBot
{
    public:
        void Load();
        void Update();

        AuctionBuyerDecision Evaluate(AuctionEntry const& auction) const;
        bool IsEnabled() const { return m_enable; }

    private:
        void Buy(AuctionEntry* auction, uint32 price);

        bool   m_enable = false;
        uint32 m_sellDelay = 3600;
        double m_maxMarkup = 0.5;
};

#define sAuctionHouseBuyerBot MaNGOS::Singleton<AuctionHouseBuyerBot>::Instance()

#endif
