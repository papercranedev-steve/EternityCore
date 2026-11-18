/*
 * Class quest chains: what a companion asks the player to help with, one entry per chain.
 *
 * Made from quest_template at startup (Load): the class quests (RequiredClasses, not breadcrumbs)
 * filed under the class in the quest log (ZoneOrSort = the class's sort) or rewarding a class
 * quest ability, linked by PrevQuestId / NextQuestInChain. Not the raid and dungeon token
 * turn-ins (AQ, Naxxramas, Zul'Gurub sets, T0.5): those are zone quests. A chain is named by its last quest (no class quest
 * continues it); race variants that end in different quests are different chains, a shared start
 * belongs to each. A chain that goes on after its last ability step is cut there: the ability
 * chain ends with that step, the rest is a gear chain (afterAbility). Its kind:
 *   Ability: some step rewards a class quest ability (Shared/ClassQuestAbilities)
 *   Gear:    otherwise, some step rewards items (fixed or a choice)
 *   (neither: not a chain, there is nothing to get)
 * Affinity (what helping gives, 0..100 scale), by default from kind and length: gear 1;
 * ability 2 (1 step), 4 (2-4 steps), 6 (5+). The world table `custom_classquest_chain` overrides
 * kind, affinity, or leaves a chain out (skip), keyed by the last quest.
 *
 * Read only after Load (any thread). Design: .kiro/docs/2026-11-18-companion-class-quests.md §6-§7.
 */

#ifndef MANGOS_BOTSHARED_CLASS_QUEST_CHAINS_H
#define MANGOS_BOTSHARED_CLASS_QUEST_CHAINS_H

#include "Common.h"
#include <string>
#include <vector>

class Player;
class Quest;

namespace BotShared
{
    namespace ClassQuestChains
    {
        enum class Kind : uint8 { Ability = 0, Gear = 1 };

        struct Chain
        {
            uint32 id = 0;              // the last quest
            uint32 classMask = 0;       // RequiredClasses of the last quest
            uint32 raceMask = 0;        // RequiredRaces of the last quest, 0 = any
            Kind kind = Kind::Gear;
            uint32 affinity = 0;
            uint32 minLevel = 0;        // the first quest's MinLevel
            std::vector<uint32> quests; // first to last (one path when it branches)
            // A gear chain that goes on after an ability chain (Defensive Stance, then a weapon):
            // its steps start after the last ability step; `after` is that ability chain's id.
            bool afterAbility = false;
            uint32 after = 0;
            std::string comment;        // from the override row
        };

        // World thread, startup, after the quests (PlayerBotMgr::Load). Again: rebuilds.
        void Load();

        std::vector<Chain> const& All();
        Chain const* Find(uint32 chainId);

        // The quests `player` does for the chain, first to last (its class and race); empty: the
        // chain is not for this player, also when every creature that starts it is hostile to its team.
        std::vector<Quest const*> StepsFor(Player const* player, Chain const& chain);

        // The chains for `player` (StepsFor not empty). Ability chains that give the same abilities
        // (one per start zone, like the five Defensive Stance chains) count once: the one whose first
        // quest giver is friendly to it, else the lowest id. Order: All()'s.
        std::vector<Chain const*> ChainsFor(Player const* player);

        // Its last quest is rewarded.
        bool IsDone(Player const* player, Chain const& chain);
    }
}

#endif
