/*
 * Puts a done quest into a character's history without doing it: the quest and the quests before
 * it in its chain count as rewarded, and (Complete) the character gets what the quest teaches and
 * its fixed reward items. No XP, money, reputation, choice items, mail or quest scripts. For a
 * companion's past before it met the player, and later for a class quest it did on its own.
 *
 * Not Player::RewardQuest: that needs a quest ender (it dereferences it) and gives XP and money.
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md (workspace root).
 */

#ifndef MANGOS_BOTSHARED_QUEST_GRANT_H
#define MANGOS_BOTSHARED_QUEST_GRANT_H

#include "Common.h"
#include <functional>
#include <vector>

class Player;
class Quest;

namespace BotShared
{
    namespace QuestGrant
    {
        // `quest` and the quests done before it (PrevQuestId, or a quest whose NextQuestInChain it
        // is), oldest first and `quest` last, only quests `fits` accepts; a branch with several
        // choices takes the first that fits. At most 20.
        std::vector<Quest const*> ChainUpTo(Quest const* quest, std::function<bool(Quest const*)> const& fits);
        // ... the quests of the player's class and race.
        std::vector<Quest const*> ChainUpTo(Player const* player, Quest const* quest);
        // Every way to `quest` when several quests lead to it (Felsteed: four starters, one per
        // capital; Felhunter: a Horde and an Alliance branch into the same last quest), each like
        // ChainUpTo. At most 16 ways.
        std::vector<std::vector<Quest const*>> AllChainsUpTo(Quest const* quest, std::function<bool(Quest const*)> const& fits);

        // The chain up to `quest` counts as rewarded (quests in the quest log are left alone).
        void MarkChainRewarded(Player* player, Quest const* quest);
        // ... exactly these quests (a way picked from AllChainsUpTo).
        void MarkStepsRewarded(Player* player, std::vector<Quest const*> const& steps);

        // What `quest` teaches (learn effects of its reward spell) and its fixed reward items (no
        // room: that item is skipped and logged). Nothing is marked.
        void GiveRewards(Player* player, Quest const* quest);

        // MarkChainRewarded and GiveRewards.
        void Complete(Player* player, Quest const* quest);
    }
}

#endif
