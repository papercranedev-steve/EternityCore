/*
 * Shared bot hands (PlayerBots/Shared): selling, repairing and buying at an NPC, through the
 * client's own handlers / Player::BuyItemFromVendor (distance, reputation, stock, price and bag
 * checks all apply). Soul companions doc §4.4 (stage S4).
 *
 * Buying: PlanPurchases spends money and bag space by Supplies priority:
 *  - Critical needs may use all the money and every free slot;
 *  - the others keep `reserve` copper (money set aside, e.g. for the class trainer) and
 *    KEEP_FREE_SLOTS free bag slots (room for loot);
 *  - when money or space is short a need is bought in part (as many vendor stacks as fit).
 */

#ifndef MANGOS_BOTSHARED_VENDOR_H
#define MANGOS_BOTSHARED_VENDOR_H

#include "Common.h"
#include "Shared/Supplies.h"
#include <vector>

class Creature;
class Player;

namespace BotShared
{
    namespace Vendor
    {
        constexpr uint32 KEEP_FREE_SLOTS = 2;

        // Gray items that sell (not quest items). Sells them all at `vendor`; returns the copper earned.
        bool HasJunk(Player const* player);
        uint32 SellJunk(Player* player, Creature* vendor);

        // What repairing everything would cost now at `npc` (its reputation discount).
        uint32 RepairCost(Player* player, Creature const* npc);
        // Repairs all through the repair handler; returns the copper spent.
        uint32 RepairAll(Player* player, Creature* npc);

        struct Purchase
        {
            uint32 itemId = 0;
            uint32 stacks = 0;      // vendor stacks (ItemPrototype::BuyCount each)
            uint32 cost = 0;        // copper
        };

        // What to buy at `vendor` for `needs` (Supplies::Missing order).
        std::vector<Purchase> PlanPurchases(Player* player, Creature* vendor,
                                            std::vector<Supplies::Need> const& needs, uint32 reserve);
        // Returns the copper spent.
        uint32 Buy(Player* player, Creature* vendor, std::vector<Purchase> const& purchases);
    }
}

#endif
