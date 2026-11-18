/*
 * Shared bot facts (PlayerBots/Shared): rogue weapon poisons. Soul companions doc §4.4 (S4).
 *
 * A rogue knows recipes (Instant Poison, Deadly Poison ...: create item spells, Shared/Crafting)
 * that make a vial; the vial's on-use spell puts the poison on a weapon (a temporary enchant).
 * Damage poisons only, Instant before Deadly (as CombatBotBaseAI picks the enchant spells for
 * bots with free supplies): Crippling, Wound and Mind-numbing do no damage, they are for PvP.
 */

#ifndef MANGOS_BOTSHARED_POISONS_H
#define MANGOS_BOTSHARED_POISONS_H

#include "Common.h"

class Item;
class Player;
struct ItemPrototype;
class SpellEntry;

namespace BotShared
{
    namespace Poisons
    {
        // How much the vial is wanted on a weapon: 0 = no damage poison (or no poison at all);
        // a damage poison the player can use now, higher = better (kind, then level).
        uint32 Worth(Player const* player, ItemPrototype const* proto);

        // The known recipe that makes the best vial, or null.
        SpellEntry const* BestRecipe(Player const* player);

        // The best vial in the backpack and bags, or null.
        Item* BestInBags(Player const* player);
    }
}

#endif
