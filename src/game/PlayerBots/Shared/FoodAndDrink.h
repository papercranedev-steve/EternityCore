/*
 * Shared bot facts (PlayerBots/Shared): food and drink. Soul companions doc §4.4 (stage S4).
 *
 * An eating / drinking spell is a seated aura (AURA_INTERRUPT_STANDING_CANCELS) that restores
 * health (SPELL_AURA_MOD_REGEN, SPELL_AURA_OBS_MOD_HEALTH) or mana (SPELL_AURA_MOD_POWER_REGEN
 * of mana, SPELL_AURA_OBS_MOD_MANA). Food and drink items cast one on use; a few do both
 * (Enriched Manna Biscuit). Party bots that get free supplies rest with the aura spells
 * 1131 / 1137 instead (PartyBotAI::StartResting), companions eat what is in their bags.
 */

#ifndef MANGOS_BOTSHARED_FOOD_AND_DRINK_H
#define MANGOS_BOTSHARED_FOOD_AND_DRINK_H

#include "Common.h"

class Item;
class Player;
struct ItemPrototype;
struct SpellEntry;

namespace BotShared
{
    namespace FoodAndDrink
    {
        enum class Kind : uint8
        {
            Food,       // health
            Drink,      // mana
        };

        // Health (Food) or mana (Drink) per second the spell restores on `player`, at the
        // server's regeneration rates; 0 when it is no eating / drinking spell of that kind.
        float RatePerSec(Player const* player, SpellEntry const* spell, Kind kind);

        // The item's on-use eating / drinking spell of that kind, or null.
        SpellEntry const* ItemSpell(ItemPrototype const* proto, Kind kind);

        // An item of that kind the player can eat / drink now (level, skill).
        bool IsUsable(Player const* player, ItemPrototype const* proto, Kind kind);

        // Of two usable items of that kind, `a` is the better one: it restores faster, or the
        // same and is worth less (the cheaper one goes first).
        bool Better(Player const* player, ItemPrototype const* a, ItemPrototype const* b, Kind kind);

        // The best usable item of that kind in the backpack and bags, or null.
        Item* BestInBags(Player const* player, Kind kind);

        // The eating / drinking aura of that kind on the player now (from any source), or null.
        SpellEntry const* Active(Player const* player, Kind kind);
    }
}

#endif
