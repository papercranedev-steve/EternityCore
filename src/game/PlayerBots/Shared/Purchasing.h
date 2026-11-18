/*
 * Shared bot facts (PlayerBots/Shared): money and bag space a shopping round may spend, by need
 * priority (Shared/Supplies). Soul companions doc §4.4.
 *  - Critical needs may use all the money and every free slot;
 *  - the others keep `reserve` copper (set aside, e.g. for the class trainer) and
 *    KEEP_FREE_SLOTS free bag slots (room for loot).
 * One Budget is shared by every shop of a round (the auction house, then the vendors), with
 * the units already planned of each goods, so two shops never buy the same need twice.
 */

#ifndef MANGOS_BOTSHARED_PURCHASING_H
#define MANGOS_BOTSHARED_PURCHASING_H

#include "Common.h"
#include "Shared/Supplies.h"
#include <map>

class Player;

namespace BotShared
{
    namespace Purchasing
    {
        constexpr uint32 KEEP_FREE_SLOTS = 2;

        struct Budget
        {
            uint32 money = 0;
            uint32 freeSlots = 0;
            uint32 reserve = 0;
            std::map<Goods::Spec, uint32> planned;      // units planned of each goods
            std::map<uint32, uint32> plannedOfItem;     // units planned of each item entry

            // The player's money and free bag slots now.
            static Budget Of(Player const* player, uint32 reserve);

            uint32 MoneyFor(Supplies::Priority priority) const;
            uint32 SlotsFor(Supplies::Priority priority) const;
            // What `need` still misses after the units planned of its goods.
            uint32 StillMissing(Supplies::Need const& need) const;
            void Spend(Goods::Spec const& goods, uint32 itemId, uint32 units, uint32 cost, uint32 slots);
        };

        // New bag slots `units` more of `proto` take, on top of the bags' and the units already
        // planned of it (the last partial stack is filled first).
        uint32 SlotsFor(Player const* player, ItemPrototype const* proto, uint32 units, uint32 plannedUnits);
    }
}

#endif
