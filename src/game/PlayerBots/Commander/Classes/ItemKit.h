/*
 * PartyBot Commander: consumables in the bags (healthstones, healing and mana potions, mana
 * gems, runes), the item counterpart of RaceKit (cooldown doc .kiro/docs/2026-11-14-cooldowns.md
 * 3). A table of item ids best first; the amounts come from each item's on-use spell.
 * WorldModelBuilder turns them into facts (SurvivalAbility with an item id: the team rescue;
 * ManaItemAbility: ManaBudgetBehavior), the arbiter uses them with BotActionType::UseItem.
 *
 * Items that share a cooldown (the spell category: potions 4, healthstones / mana gems / runes 30
 * in this database) exclude each other: only the best held item of each category is listed.
 * Healthstones and mana gems / runes are in the same category, so using one also locks the
 * other; the cooldown points of the one used stand for that.
 */

#ifndef MANGOS_COMMANDER_ITEM_KIT_H
#define MANGOS_COMMANDER_ITEM_KIT_H

#include "Common.h"
#include <vector>

class PartyBotAI;
class SpellEntry;
struct ItemPrototype;

namespace Commander
{
    namespace ItemKit
    {
        enum class Use : uint8
        {
            Heal,   // heals the user (healthstones, healing potions): the team rescue
            Mana,   // restores the user's mana (mana potions, gems, runes): ManaBudgetBehavior
        };

        struct Consumable
        {
            ItemPrototype const* proto = nullptr;
            SpellEntry const* spell = nullptr;  // its on-use spell
            uint32 cooldownMs = 0;              // the longest of the item's, its category's, the spell's
        };

        // The best held item of each shared cooldown for `use`, best first.
        std::vector<Consumable> Held(PartyBotAI const& bot, Use use);
        bool HoldsAny(PartyBotAI const& bot, Use use);

        // Average of an effect's random range: base points + 1 .. base points + die sides.
        float AverageValue(SpellEntry const* spell, uint32 effectIndex);
    }
}

#endif
