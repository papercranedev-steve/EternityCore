/*
 * Abilities a class gets from a class quest, not from a trainer: what the quest's reward spell
 * teaches (Defensive Stance with Sunder Armor and Taunt, Summon Voidwalker, Tame Beast with Call
 * Pet and Dismiss Pet, Bear Form, Poisons...) and the totem tools it gives (Earth, Fire, Water,
 * Air Totem). Read from quest_template: a quest with RequiredClasses whose reward spell has
 * SPELL_EFFECT_LEARN_SPELL effects or whose fixed reward items are some spell's totem tool.
 * Quests that teach the same thing in different places (one per race or start zone) are the
 * variants of one ability.
 *
 * Built once, on first use (thread-safe), from the quest and spell stores.
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md (workspace root).
 */

#ifndef MANGOS_BOTSHARED_CLASS_QUEST_ABILITIES_H
#define MANGOS_BOTSHARED_CLASS_QUEST_ABILITIES_H

#include "Common.h"
#include <vector>

class Player;
class Quest;

namespace BotShared
{
    namespace ClassQuestAbilities
    {
        struct Ability
        {
            // The first spell taught, or the first tool when it teaches none: the same for all
            // variants (a stable id, e.g. for a seeded roll).
            uint32 key = 0;
            uint32 classMask = 0;               // classes of all variants
            std::vector<Quest const*> variants; // the quests that reward it
            std::vector<uint32> spells;         // spells taught (learn effects of the reward spell)
            std::vector<uint32> tools;          // totem tools among the fixed reward items
        };

        std::vector<Ability> const& All();

        // The ability `questId` rewards, nullptr when it rewards none.
        Ability const* AbilityOf(uint32 questId);

        // The variant `player` would do: its class, a race it may take it with, and a quest giver
        // that is not hostile to its team when there is a choice. nullptr: none for this player.
        Quest const* VariantFor(Player const* player, Ability const& ability);

        // A variant was rewarded (done before, or granted).
        bool IsEarned(Player const* player, Ability const& ability);

        // Takes away what the ability gives: its spells (with their higher ranks), the spells of a
        // skill one of them teaches (Poisons: the poison recipes), and its tools. For a bot that got
        // it without the quest (premade spec templates teach quest spells).
        void Revoke(Player* player, Ability const& ability);
    }
}

#endif
