/*
 * Shared bot hands (PlayerBots/Shared): handing items from one character's bags to another's
 * without a trade window (a bot crafting with the player's materials: Life/Errands/
 * CraftForPlayer; the player and his companions' bags: `.partybot giveslot / takeslot /
 * passslot`). When the receiver's bags are full, the items can go by mail instead.
 *
 * Items of a temporary bot (saving disabled) were never saved: they become new rows for the
 * receiver, as in `.partybot giveslot`. Both sides are saved after each hand-over.
 *
 * Map thread of both characters (the same map).
 */

#ifndef MANGOS_BOTSHARED_HANDOVER_H
#define MANGOS_BOTSHARED_HANDOVER_H

#include "Common.h"
#include "Player.h"
#include <string>

namespace BotShared
{
    namespace Handover
    {
        enum class Result : uint8
        {
            Bags,       // in the receiver's bags
            Mailed,     // sent by mail (its bags were full)
            Failed,     // nothing moved (no room and no mail, or not enough items)
        };

        // `to` has room in its bags for `count` new items `itemId`.
        bool Fits(Player const* to, uint32 itemId, uint32 count);

        // `count` of `itemId` out of `from`'s bags into `to`'s (new items there; stacks are
        // plain goods, nothing of them is lost). `mailSubject` empty: no mail, Failed when full.
        Result MoveCount(Player* from, uint32 itemId, uint32 count, Player* to, std::string const& mailSubject);

        // This item of `from`'s bags, as it is (its maker kept), to `to`.
        Result MoveItem(Player* from, Item* item, Player* to, std::string const& mailSubject);

        // Where an item of `from` goes when it is put on one place of `to`'s inventory (the
        // addon's drop on a slot), as in your own bags:
        //  - a bag slot: there, or merged into the stack there; another item lies there and this
        //    one lies in a bag slot (not worn, not in the bank): the two swap places
        //  - an equipment slot: into `to`'s bags, then put on (what was worn there takes the bag
        //    place it left)
        //  - a bag slot it can't go to, or no place (bag NULL_BAG): anywhere in `to`'s bags
        // No mail: a full receiver refuses.
        struct Placement
        {
            ItemPosCountVec dest;           // in `to`'s inventory
            Item* back = nullptr;           // `to`'s item going to the place this one left
            ItemPosCountVec backDest;       // in `from`'s inventory
            uint8 equipSlot = NULL_SLOT;    // put on afterwards
        };

        // EQUIP_ERR_OK, or why not: `to` can't wear it there (then placement.equipSlot stays
        // NULL_SLOT), or no room in `to`'s bags.
        InventoryResult Plan(Player const* from, Item* item, Player const* to, uint8 bag, uint8 slot, Placement& placement);

        // Moves it (and the one coming back) as planned; both sides are saved. Returns the item
        // where it landed in `to`'s inventory (or the stack it was merged into).
        Item* Place(Player* from, Item* item, Player* to, Placement const& placement);

        // Saves the bags and money of a character that is saved (not a temporary bot).
        void Save(Player* player);
    }
}

#endif
