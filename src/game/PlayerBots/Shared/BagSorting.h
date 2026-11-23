/*
 * Shared bot facts and hands (PlayerBots/Shared): what a bot does with each thing in its bags,
 * and making room for something new. User 2026-11-23, .kiro/docs/2026-11-23-bot-bags.md.
 *
 * Fate of a bag item, the first that applies:
 *   Keep   quest items (and items a quest in the log wants), keys, conjured or timed items,
 *          what its supplies want (Supplies::KeptGoods: reagents, ammo, food, drink, tools),
 *          health and mana potions and gems it can use (the Commander drinks them);
 *   Vendor gray items that sell; Trash: gray items that do not (only destroyed for room);
 *   Keep   what a player gave it (Shared/GivenItems), an upgrade not worn yet, a bigger bag,
 *          unbound gear and bags while the player manages its bags (Rules::keepUnboundGear),
 *          what neither a vendor nor the auction house pays for (the hearthstone);
 *   Market tradable, in the auction house price table, worth more there after the house's cut
 *          (MARKET_CUT_PCT) than at a vendor, while the AH buyer bot buys (AHBuyer.Enable);
 *   Vendor the rest (bound gear it replaced, items the auction house does not take).
 *
 * MakeRoom: destroys bag items that are not kept, the cheapest first (WorthOf), each only when
 * worth less than what the room is for, until enough slots are free.
 *
 * Map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_BAG_SORTING_H
#define MANGOS_BOTSHARED_BAG_SORTING_H

#include "Common.h"
#include <vector>

class Item;
class Player;

namespace BotShared
{
    namespace BagSorting
    {
        // The auction house cut counted when comparing with a vendor: the neutral houses' 15 %
        // (the faction houses take 5 %).
        constexpr uint32 MARKET_CUT_PCT = 15;
        // Worth of something that gets room whatever it costs (quest items, quest rewards).
        constexpr uint32 PRICELESS = 0xFFFFFFFF;

        enum class Fate : uint8
        {
            Keep,
            Vendor,
            Market,     // the auction house; a vendor when the trip has no auction house stop
            Trash,
        };

        struct Rules
        {
            // The player manages this bot's bags (a companion's ManageBags gate): unbound gear
            // is his business, the bot keeps it.
            bool keepUnboundGear = false;
        };

        Fate FateOf(Player const* player, Item const* item, Rules const& rules);
        // Copper the whole stack brings where its fate sells it (the auction house price for
        // Market, else a vendor's).
        uint32 WorthOf(Player const* player, Item const* item, Rules const& rules);
        // A new item that is not in the bags yet: the auction house price, else a vendor's.
        uint32 WorthOf(uint32 itemId, uint32 count);

        // Bag items of these fates, in bag order.
        std::vector<Item*> WithFate(Player const* player, Rules const& rules, std::vector<Fate> const& fates);
        bool HasAny(Player const* player, Rules const& rules, std::vector<Fate> const& fates);

        // Destroys items until `slots` bag slots are free (see the header). True: they are.
        bool MakeRoom(Player* player, Rules const& rules, uint32 slots, uint32 worth);
        // Room for `count` new `itemId` (stacks it would join count): destroys as MakeRoom.
        bool MakeRoomFor(Player* player, Rules const& rules, uint32 itemId, uint32 count, uint32 worth);
        // MakeRoomFor would succeed (nothing destroyed).
        bool CanMakeRoomFor(Player const* player, Rules const& rules, uint32 itemId, uint32 count, uint32 worth);
    }
}

#endif
