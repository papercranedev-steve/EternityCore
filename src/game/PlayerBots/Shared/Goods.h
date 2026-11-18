/*
 * Shared bot facts (PlayerBots/Shared): which items satisfy a supply need. Soul companions doc
 * §4.4 (stage S4).
 *
 * A need names either one exact item (a spell reagent, the Ankh) or a kind of goods any of
 * several items satisfy (ammo for the ranged weapon, food, drink, the pet's food). The bags
 * count every matching item; at a vendor the best matching offer is bought (Shared/Vendor).
 * A new kind is one enum value and its three cases here.
 */

#ifndef MANGOS_BOTSHARED_GOODS_H
#define MANGOS_BOTSHARED_GOODS_H

#include "Common.h"
#include <string>

class Player;
struct ItemPrototype;

namespace BotShared
{
    namespace Goods
    {
        enum class Kind : uint8
        {
            Item,       // id: the item entry
            Ammo,       // id: the projectile subclass (ITEM_SUBCLASS_ARROW / BULLET), usable ones
            Food,       // the player eats it (Shared/FoodAndDrink)
            Drink,      // the player drinks it
            PetFood,    // the player's hunter pet eats it (Shared/PetFood)
        };

        struct Spec
        {
            Kind kind = Kind::Item;
            uint32 id = 0;

            bool operator<(Spec const& other) const
            {
                return kind != other.kind ? kind < other.kind : id < other.id;
            }
        };

        inline Spec Exact(uint32 itemId) { return { Kind::Item, itemId }; }
        inline Spec AmmoOf(uint32 subClass) { return { Kind::Ammo, subClass }; }
        inline Spec Of(Kind kind) { return { kind, 0 }; }

        // `proto` is of these goods and the player can use it now.
        bool Matches(Player const* player, Spec const& spec, ItemPrototype const* proto);

        // Of two matching items, `a` is the better buy.
        bool Better(Player const* player, Spec const& spec, ItemPrototype const* a, ItemPrototype const* b);

        // Units of these goods in the backpack and bags.
        uint32 CountInBags(Player const* player, Spec const& spec);

        // For logs: "item 17020", "ammo", "food" ...
        std::string Describe(Spec const& spec);
    }
}

#endif
