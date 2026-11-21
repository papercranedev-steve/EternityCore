/*
 * Shared bot hands (PlayerBots/Shared): making an item with a spell the player knows (a
 * recipe: SPELL_EFFECT_CREATE_ITEM). Soul companions doc §4.4 (stage S4).
 *
 * Conjured food and water, rogue poisons now; professions later (they add workplaces: a spell
 * focus such as a forge or a fire, which a bot on the road does not have, so OnTheRoad says no).
 * The cast is the player's own (not triggered): the server takes the reagents and the power and
 * checks tools, cooldown and bag room as for a player.
 *
 * What to make and how many is the caller's (Shared/Supplies::ToMake), when to make it too
 * (Life/Errands/CraftRun).
 */

#ifndef MANGOS_BOTSHARED_CRAFTING_H
#define MANGOS_BOTSHARED_CRAFTING_H

#include "Common.h"
#include "SpellDefines.h"
#include <string>
#include <vector>

class Player;
class SpellEntry;

namespace BotShared
{
    namespace Crafting
    {
        struct Product
        {
            uint32 itemId = 0;      // 0 = the spell makes no item
            uint32 perCast = 0;     // items one cast makes (at least 1)
        };

        // The item the recipe makes (its first create item effect).
        Product ProductOf(SpellEntry const* recipe);

        // Item-making spells the player knows (active, not disabled), every rank.
        std::vector<SpellEntry const*> KnownRecipes(Player const* player);
        // Those of skill line `skill` (SkillOf).
        std::vector<SpellEntry const*> KnownRecipesOf(Player const* player, uint32 skill);

        // Made anywhere: no spell focus (forge, anvil, fire) is needed.
        bool OnTheRoad(SpellEntry const* recipe);

        // Casts the reagents in the bags are enough for (no reagents: unlimited, UINT32_MAX).
        uint32 CastsFromBags(Player const* player, SpellEntry const* recipe);

        // The tools the recipe needs (Totem: Blacksmith Hammer, Mining Pick ...) are in the bags.
        bool HasTools(Player const* player, SpellEntry const* recipe);

        // One cast now, through the player's spell path. Checked first, so a recipe that cannot
        // be made does not start a cast: reagents and tools in the bags, room for the product.
        SpellCastResult Craft(Player* player, SpellEntry const* recipe);

        // The skill line the recipe is of (SkillLineAbility), 0 = none.
        uint32 SkillOf(SpellEntry const* recipe);
        // How likely making it raises a skill of `skillValue` (Player::UpdateCraftSkill), as the
        // trade skill window colours it: 0 orange, 1 yellow, 2 green, 3 grey.
        uint8 Difficulty(SpellEntry const* recipe, uint32 skillValue);
        // What it makes, to group a recipe list: alchemy "potion" (heals, restores, protects),
        // "elixir", "flask" (an aura of 2 hours or more), "transmute" (trade goods); anything
        // else "<item class>.<subclass>" of the product.
        std::string CategoryOf(SpellEntry const* recipe);

        struct Reagent
        {
            uint32 itemId = 0;
            uint32 count = 0;
        };
        std::vector<Reagent> ReagentsOf(SpellEntry const* recipe);
    }
}

#endif
