/*
 * Shared bot facts (PlayerBots/Shared): the items a player gave a saved bot (a trade, or put
 * into its bags with `.partybot takeslot`). A bot never sells or destroys them on its own,
 * except gray junk (user 2026-11-23, .kiro/docs/2026-11-23-bot-bags.md; Shared/BagSorting).
 *
 * One row per item guid in `custom_bot_given_item` (characters DB), read once on first use.
 * Rows of items that no longer exist are dropped at that load. Temporary bots (saving
 * disabled) are not marked: their items are never saved.
 *
 * Any thread (a lock guards the set).
 */

#ifndef MANGOS_BOTSHARED_GIVEN_ITEMS_H
#define MANGOS_BOTSHARED_GIVEN_ITEMS_H

#include "Common.h"

class Item;
class Player;

namespace BotShared
{
    namespace GivenItems
    {
        // `owner` got `item` from a player. Nothing for a temporary bot or gray junk.
        void Mark(Player const* owner, Item const* item);
        bool IsGiven(Item const* item);
    }
}

#endif
