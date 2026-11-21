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
 *  - the hunter pet's food (Shared/PetFood);
 *  - what it makes itself (Need::recipe, Shared/Crafting): food and drink from a recipe made on
 *    the road with nothing or vendor reagents (a mage's Conjure Food / Water; then that food or
 *    drink is not bought), a rogue's best damage poison (Shared/Poisons). The reagents for the
 *    missing products are bought at the row's priority.
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
#include "ObjectGuid.h"
#include <set>
#include <unordered_set>
#include <vector>

class Player;
class SpellEntry;

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
            // Made with this recipe (Shared/Crafting), not bought; null = bought.
            SpellEntry const* recipe = nullptr;
            // Fewer than this in the bags: worth a town trip of its own, whatever its priority
            // (user 2026-11-21: a mage who knows a portal spell and runs low on Rune of Portals
            // asks to go buy them in town). 0 = only with other business.
            uint32 tripBelow = 0;
            uint32 Missing() const { return target > have ? target - have : 0; }
            bool WantsTrip() const { return tripBelow && have < tripBelow; }
        };

        // What to buy. Sorted by priority (most important first); only needs with something
        // missing. Goods the bot makes itself are not bought ("makes it, does not buy it": a
        // mage's conjured food and water); the reagents of what it makes are.
        std::vector<Need> Missing(Player const* player);

        // What to make (Need::recipe set), one row per goods (the biggest target); only needs
        // with something missing. Most important first.
        std::vector<Need> ToMake(Player const* player);

        // Some vendor in the world sells it (index built once).
        bool IsSoldByVendors(uint32 itemId);
        // Every item some vendor sells (the same index).
        std::unordered_set<uint32> const& SoldByVendors();

        // Goods kinds a teammate gives this player (user 2026-11-20: with a mage in the party
        // the others ask it for food and drink, Life/Errands/AskProvisions): Missing leaves out
        // the bought rows of these kinds (the critical ones too: no town trip for them). Set
        // by the player's own bot every update; empty = it buys everything. Any thread.
        void SetProvided(ObjectGuid player, std::set<Goods::Kind> const& kinds);
    }
}

#endif
