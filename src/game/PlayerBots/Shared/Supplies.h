/*
 * Shared bot facts (PlayerBots/Shared): the consumables a bot wants to keep in its bags, and how
 * badly. Soul companions doc §4.4 (stage S4).
 *
 * Each source adds its needs (Supplies.cpp SOURCES; a new source is one function and one row):
 *  - reagents of the spells it knows (highest rank of each spell only), when some vendor sells
 *    them: Arcane Powder, Holy Candle, Symbol of Kings, Wild Berries / Thornroot, Rebirth seeds,
 *    Rune of Teleportation, Light Feather, Flash Powder ... Crafting and poison making (spells
 *    that create or enchant items) and reagents no vendor sells (Soul Shard) are left out;
 *  - extra items that are not a spell's reagent (EXTRA table: the Ankh for Reincarnation);
 *  - ammo for the ranged weapon it wears;
 *  - food, and drink when it has mana (Shared/FoodAndDrink: what PartyBotAI rests with);
 *  - the hunter pet's food (Shared/PetFood).
 * What satisfies a need is a Goods::Spec (one item, or a kind such as "food it can eat").
 *
 * Priority decides what money and bag space go to first (Shared/Vendor::PlanPurchases):
 *  - Critical: without it the bot cannot do its job or a party member stays dead (ammo,
 *    resurrection reagents, the first few food and drink);
 *  - Important: what the party expects (buff reagents, the rest of the food and drink);
 *  - Optional: comfort (teleport runes, slow fall).
 * A goods kind may have rows of several priorities (food: 5 critical, up to 20 important);
 * each row's target counts the whole stock, not on top of the row before.
 * (Totems are quest rewards: not here.)
 */

#ifndef MANGOS_BOTSHARED_SUPPLIES_H
#define MANGOS_BOTSHARED_SUPPLIES_H

#include "Common.h"
#include "Shared/Goods.h"
#include <vector>

class Player;

namespace BotShared
{
    namespace Supplies
    {
        enum class Priority : uint8
        {
            Critical = 0,
            Important = 1,
            Optional = 2,
        };

        struct Need
        {
            Goods::Spec goods;
            uint32 target = 0;          // units wanted in the bags
            uint32 have = 0;            // units in the bags now
            Priority priority = Priority::Important;
            uint32 Missing() const { return target > have ? target - have : 0; }
        };

        // Sorted by priority (most important first); only needs with something missing.
        std::vector<Need> Missing(Player const* player);

        // Some vendor in the world sells it (index built once).
        bool IsSoldByVendors(uint32 itemId);
    }
}

#endif
