/*
 * Shared bot facts (PlayerBots/Shared): what an item is worth to a player who sells it.
 * The auction house price is the reference unit price the AH buyer bot pays for players'
 * auctions (AuctionHouseBotMgr::GetReferenceUnitPrice, the `auctionhousebot` table); an item
 * not in that table is never bought there. Otherwise a vendor's price (item_template
 * sell_price). Any thread (both tables are read only after startup).
 */

#ifndef MANGOS_BOTSHARED_MARKET_VALUE_H
#define MANGOS_BOTSHARED_MARKET_VALUE_H

#include "Common.h"

namespace BotShared
{
    namespace MarketValue
    {
        // Copper one item sells for at the auction house, 0 = it does not sell there.
        uint32 AuctionPrice(uint32 itemId);
        // The auction house price, else what a vendor pays; 0 = neither.
        uint32 WorthOf(uint32 itemId);
    }
}

#endif
