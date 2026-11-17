/*
 * Bot daily life (PlayerBots/Life): who picks a quest reward when the bot turns a quest in.
 * The turn-in errand (QuestTurnIn.h) asks every tick until the chooser answers; after
 * QuestTurnIn::CHOICE_WAIT_MS it picks by itself (Shared/QuestRewards::BestChoice), so a
 * player who never answers does not keep the bot standing at the quest giver.
 *
 * AutoRewardChooser: the bot picks at once (default). Companions/ adds one that can ask the
 * player (soul companions doc §4.3: tied to affinity later).
 */

#ifndef MANGOS_LIFE_REWARD_CHOOSER_H
#define MANGOS_LIFE_REWARD_CHOOSER_H

#include "Common.h"

class Player;
class Quest;

namespace Life
{
    class IRewardChooser
    {
    public:
        virtual ~IRewardChooser() = default;
        // True with `index` (into Quest::RewChoiceItemId) set: decided. False: not yet.
        // Only asked for quests with more than one reward to choose from.
        virtual bool Choose(Player* me, Quest const* quest, uint32& index) = 0;
        // The turn-in is over (rewarded, gone, timed out): forget anything asked for `questId`.
        virtual void Done(Player* /*me*/, uint32 /*questId*/) {}
    };

    class AutoRewardChooser final : public IRewardChooser
    {
    public:
        bool Choose(Player* me, Quest const* quest, uint32& index) override;
    };
}

#endif
