/*
 * Shared bot hands (PlayerBots/Shared): handing items from one character's bags to another's
 * without a trade window (a bot crafting with the player's materials: Life/Errands/
 * CraftForPlayer). When the receiver's bags are full, the items can go by mail instead.
 *
 * Items of a temporary bot (saving disabled) were never saved: they become new rows for the
 * receiver, as in `.partybot giveslot`. Both sides are saved after each hand-over.
 *
 * Map thread of both characters (the same map).
 */

#ifndef MANGOS_BOTSHARED_HANDOVER_H
#define MANGOS_BOTSHARED_HANDOVER_H

#include "Common.h"
#include <string>

class Item;
class Player;

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

        // Saves the bags and money of a character that is saved (not a temporary bot).
        void Save(Player* player);
    }
}

#endif
