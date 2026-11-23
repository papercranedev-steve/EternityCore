/*
 * Shared bot hands (PlayerBots/Shared): selling, repairing and buying at an NPC, through the
 * client's own handlers / Player::BuyItemFromVendor (distance, reputation, stock, price and bag
 * checks all apply). Soul companions doc §4.4 (stage S4).
 *
 * Buying: PlanPurchases spends a Purchasing::Budget by Supplies priority (critical needs may use
 * everything; the others keep the reserve and free slots). When money or space is short a need
 * is bought in part (as many vendor stacks as fit). The budget may be shared with other shops of
 * the same round (the auction house first: Life/Errands/Trip/ShoppingPlanner).
 */

#ifndef MANGOS_BOTSHARED_VENDOR_H
#define MANGOS_BOTSHARED_VENDOR_H

#include "Common.h"
#include "Shared/Purchasing.h"
#include "Shared/Supplies.h"
#include <vector>

class Creature;
class Item;
class Player;

namespace BotShared
{
    namespace Vendor
    {
        constexpr uint32 KEEP_FREE_SLOTS = Purchasing::KEEP_FREE_SLOTS;

        // Sells these bag items (whole stacks; what to sell: Shared/BagSorting) at `vendor`;
        // returns the copper earned. `stacksSold`: how many stacks it offered (one click each in
        // the window).
        uint32 Sell(Player* player, Creature* vendor, std::vector<Item*> const& items, uint32* stacksSold = nullptr);

        // What repairing everything would cost now at `npc` (its reputation discount).
        uint32 RepairCost(Player* player, Creature const* npc);
        // The same at no NPC in particular (no discount): money to keep for it.
        uint32 RepairCost(Player const* player);
        // What `needs` (Missing units) cost at list price: the item a vendor would sell for each
        // (the best that matches, Goods::Better, of every item some vendor sells). Money to keep.
        uint32 ListPrice(Player const* player, std::vector<Supplies::Need> const& needs);
        // Repairs all through the repair handler; returns the copper spent.
        uint32 RepairAll(Player* player, Creature* npc);

        // The vendor offers something of these goods the player can use.
        bool Sells(Player* player, Creature* vendor, Goods::Spec const& goods);
        // A vendor's price of one unit of the item (no reputation discount).
        uint32 UnitPrice(ItemPrototype const* proto);

        struct Purchase
        {
            uint32 itemId = 0;
            uint32 stacks = 0;      // vendor stacks (ItemPrototype::BuyCount each)
            uint32 cost = 0;        // copper
        };

        // What to buy at `vendor` for `needs` (Supplies::Missing order), spending `budget`.
        std::vector<Purchase> PlanPurchases(Player* player, Creature* vendor,
                                            std::vector<Supplies::Need> const& needs, Purchasing::Budget& budget);
        // With a budget of the player's money and bags now.
        std::vector<Purchase> PlanPurchases(Player* player, Creature* vendor,
                                            std::vector<Supplies::Need> const& needs, uint32 reserve);
        // Returns the copper spent.
        uint32 Buy(Player* player, Creature* vendor, std::vector<Purchase> const& purchases);
    }
}

#endif
