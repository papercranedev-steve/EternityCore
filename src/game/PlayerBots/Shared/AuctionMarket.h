/*
 * Shared bot facts and hands (PlayerBots/Shared): buying supplies at the auction house, as a
 * client does. Soul companions doc §4.4 (stage S4, the town trip).
 *
 * Facts: the buyout listings of an auctioneer's house that satisfy a need's goods (Goods.h),
 * cheapest per unit first. Not the bot's own, not locked to it, no higher price per unit than a
 * vendor asks for the same item (an item vendors do not sell has no cap but the budget).
 * PlanBuys spends a Purchasing::Budget like Vendor::PlanPurchases: whole listings, by need
 * priority, a listing a little over what is missing is fine (MAX_OVERSHOOT_PCT), never twice
 * what is missing.
 * Hands: Buyout queues the client's bid packet (CMSG_AUCTION_PLACE_BID with the buyout price;
 * the handler runs on the world thread: every check of a player's bid applies). The item comes
 * by mail at once (auction won mail, no delivery delay): Shared/Mailbox takes it.
 *
 * Reads the auction house from the bot's map thread: the world thread changes it only outside
 * the map update (World::Update: auctions and sessions first, then the maps).
 */

#ifndef MANGOS_BOTSHARED_AUCTION_MARKET_H
#define MANGOS_BOTSHARED_AUCTION_MARKET_H

#include "Common.h"
#include "Shared/Purchasing.h"
#include "Shared/Supplies.h"
#include <set>
#include <vector>

class Creature;
class Item;
class Player;

namespace BotShared
{
    namespace AuctionMarket
    {
        constexpr uint32 MAX_OVERSHOOT_PCT = 50;

        struct Listing
        {
            uint32 auctionId = 0;
            uint32 itemId = 0;
            uint32 count = 0;
            uint32 buyout = 0;      // copper for the whole listing
        };

        // Buyout listings of `auctioneer`'s house for `goods`, cheapest per unit first.
        std::vector<Listing> Find(Player* player, Creature* auctioneer, Goods::Spec const& goods);

        // What to buy at `auctioneer` for `needs` (Supplies::Missing order), spending `budget`.
        // `taken`: auction ids already planned (by an earlier call of the same round), added to.
        std::vector<Listing> PlanBuys(Player* player, Creature* auctioneer, std::vector<Supplies::Need> const& needs,
                                      Purchasing::Budget& budget, std::set<uint32>& taken);

        // Queues the buyout bids (the bot must stand at the auctioneer). Returns the copper the
        // listings ask for; what was really paid shows in the bot's money next tick.
        uint32 Buyout(Player* player, Creature* auctioneer, std::vector<Listing> const& listings);

        // Selling (user 2026-11-23, .kiro/docs/2026-11-23-bot-bags.md): each item at the AH bot's
        // reference price (MarketValue::AuctionPrice, the price the AH buyer bot pays, so it
        // sells): buyout = unit price x count, start bid POST_BID_PCT of it, POST_HOURS. Queues
        // the client's sell packet (CMSG_AUCTION_SELL_ITEM, world thread: every check of a
        // player's auction applies, the deposit is paid then). Items whose deposit the money left
        // cannot pay are skipped. The money comes by mail when it sells (Shared/Mailbox).
        // Returns how many were posted.
        constexpr uint32 POST_BID_PCT = 75;     // as launcher/ahbot-prices.ps1 BidRatio
        constexpr uint32 POST_HOURS = 24;
        uint32 Post(Player* player, Creature* auctioneer, std::vector<Item*> const& items);
    }
}

#endif
