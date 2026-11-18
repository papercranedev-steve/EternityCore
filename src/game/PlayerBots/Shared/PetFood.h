/*
 * Shared bot facts (PlayerBots/Shared): what a hunter's pet eats. Soul companions doc §4.4
 * (stage S4). The server's rules: the pet's family diet (Pet::HaveInDiet) and the food's level
 * against the pet's (Pet::GetCurrentFoodBenefitLevel: nothing from food 15+ levels below it).
 * Feeding itself is Feed Pet on the item (CombatBotBaseAI::FeedHunterPet).
 */

#ifndef MANGOS_BOTSHARED_PET_FOOD_H
#define MANGOS_BOTSHARED_PET_FOOD_H

#include "Common.h"

class Item;
class Pet;
class Player;
struct ItemPrototype;

namespace BotShared
{
    namespace PetFood
    {
        // The player's hunter pet (alive or dead), or null.
        Pet const* HunterPet(Player const* player);

        // Happiness one feed of it gives `pet` per tick; 0 when the pet does not eat it.
        uint32 Benefit(Pet const* pet, ItemPrototype const* proto);

        // Of two items the pet eats, `a` is the better one: more benefit, or the same and cheaper.
        bool Better(Pet const* pet, ItemPrototype const* a, ItemPrototype const* b);

        // The best food for `pet` in the backpack and bags, or null.
        Item* BestInBags(Player const* player, Pet const* pet);
    }
}

#endif
