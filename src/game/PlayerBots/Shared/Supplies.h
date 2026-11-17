/*
 * Shared bot facts (PlayerBots/Shared): the consumables a bot wants to keep in its bags, and how
 * badly. Soul companions doc §4.4 (stage S4).
 *
 * What counts:
 *  - reagents of the spells it knows (highest rank of each spell only), when some vendor sells
 *    them: Arcane Powder, Holy Candle, Symbol of Kings, Wild Berries / Thornroot, Rebirth seeds,
 *    Rune of Teleportation, Light Feather, Flash Powder ... Crafting and poison making (spells
 *    that create or enchant items) and reagents no vendor sells (Soul Shard) are left out;
 *  - extra items that are not a spell's reagent (EXTRA table: the Ankh for Reincarnation);
 *  - ammo for the ranged weapon it wears.
 * (Totems are quest rewards and food / drink are eaten from aura spells for now: not here yet.)
 *
 * Priority decides what money and bag space go to first (Shared/Vendor::PlanPurchases):
 *  - Critical: without it the bot cannot do its job or a party member stays dead (ammo,
 *    resurrection reagents);
 *  - Important: what the party expects (buff reagents);
 *  - Optional: comfort (teleport runes, slow fall).
 * Each has a target stock (casts' worth, one stack at most).
 */

#ifndef MANGOS_BOTSHARED_SUPPLIES_H
#define MANGOS_BOTSHARED_SUPPLIES_H

#include "Common.h"
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
            uint32 itemId = 0;          // 0: ammo of `ammoSubClass`, the vendor's best usable one
            uint32 ammoSubClass = 0;
            uint32 target = 0;          // units wanted in the bags
            uint32 have = 0;            // units in the bags now
            Priority priority = Priority::Important;
            uint32 Missing() const { return target > have ? target - have : 0; }
        };

        // Sorted by priority (most important first); only needs with something missing.
        std::vector<Need> Missing(Player const* player);

        // Some vendor in the world sells it (index built once).
        bool IsSoldByVendors(uint32 itemId);

        // Ammo of this subclass the player can use now, all of it in its bags.
        uint32 AmmoInBags(Player const* player, uint32 ammoSubClass);
    }
}

#endif
