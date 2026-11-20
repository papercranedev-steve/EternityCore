/*
 * Shared bot facts (PlayerBots/Shared): what a mage or a warlock makes for someone else (user
 * 2026-11-20): conjured drink and food, a healthstone. Who gives to whom and when is
 * Life/Errands/GiveProvisions and AskProvisions; this only says what fits whom.
 *
 *  - Only conjured items (ITEM_FLAG_CONJURED: not a cook's dishes, not first aid).
 *  - The product is for the taker: of the giver's known recipes (every rank), the best item the
 *    taker can use now (a level 20 taker gets the water of its level, not the mage's best).
 *  - Short: below a stack (STACK) of drink (only with mana) or food it can use; no healthstone.
 *  - A gift is GIFT_STACKS stacks of drink and food, one healthstone (unique: one per player).
 *
 * Map thread of the giver (the taker is on its map).
 */

#ifndef MANGOS_BOTSHARED_PROVISIONS_H
#define MANGOS_BOTSHARED_PROVISIONS_H

#include "Common.h"
#include <vector>

class Item;
class Player;
class SpellEntry;

namespace BotShared
{
    namespace Provisions
    {
        enum class Kind : uint8
        {
            Drink,
            Food,
            Healthstone,
        };

        constexpr uint32 STACK = 20;
        constexpr uint32 GIFT_STACKS = 2;
        // A bot taker asks a mage teammate instead of buying while the conjured item restores
        // at least this share of the best food / drink a vendor sells it (user 2026-11-20:
        // "凑活能用" is fine, far worse it buys). Conjured items match the vendor tier of
        // their level: one rank behind is 60 % (Fresh Water vs Melon Juice), two 24-42 %.
        constexpr float GOOD_ENOUGH_SHARE = 0.5f;

        // The taker has a use for it: drink with mana; food and a healthstone always.
        bool Uses(Player const* taker, Kind kind);
        // Units the taker carries that it can use now (any healthstone).
        uint32 Held(Player const* taker, Kind kind);
        // Uses it and is short of it.
        bool Short(Player const* taker, Kind kind);
        // Units handed over at once.
        uint32 GiftCount(Kind kind);

        // The giver's recipe for the best item of this kind the taker can use now, which the
        // giver can make on the road now (reagents in its bags) or holds a gift of; null.
        SpellEntry const* RecipeFor(Player const* giver, Player const* taker, Kind kind);
        // The item that recipe makes.
        uint32 ProductOf(SpellEntry const* recipe);
        // The player knows a recipe of this kind for itself (a mage's water, a warlock's
        // healthstone; reagents or not): it is not given nor asked for one.
        bool MakesOwn(Player const* player, Kind kind);
        // The product (drink or food) restores at least GOOD_ENOUGH_SHARE of the best one any
        // vendor sells that the taker can use (FoodAndDrink::RatePerSec). Healthstones: always.
        bool GoodEnough(Player const* taker, Kind kind, uint32 itemId);
        // That share (1 when no vendor sells anything better), for logs and reminders.
        float ShareOfBought(Player const* taker, Kind kind, uint32 itemId);

        // Of `kinds`, those the taker is short of and the giver can make for it.
        std::vector<Kind> Wanted(Player const* giver, Player const* taker, std::vector<Kind> const& kinds);

        // The stacks of `itemId` in the giver's bags for a gift of `count`: the fullest first,
        // at most `maxStacks`.
        std::vector<Item*> StacksFor(Player const* giver, uint32 itemId, uint32 count, uint32 maxStacks);

        bool IsHealthstone(uint32 itemId);
    }
}

#endif
