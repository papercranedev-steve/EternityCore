/*
 * Companions: who picks a companion's quest reward (Life/Errands/RewardChooser.h).
 *
 *  - RewardMode::Auto (default): the companion picks (Shared/QuestRewards::BestChoice).
 *  - RewardMode::AskPlayer (".partybot rewardmode <name> ask"; later unlocked by affinity):
 *    the companion asks in party chat (Phrases "reward.ask") and opens a request
 *    (Life/Requests, intent RewardChoice) whose options are the reward items
 *    ("item:<id>[:<count>]", the addon shows them in the client's language). It waits while
 *    the question is open, also while it waits its turn behind the leader's earlier questions
 *    (the turn-in waits up to QuestTurnIn::CHOICE_WAIT_MS); no answer: its own pick (the fallback). No human leader, or another question open: its own pick at once.
 *
 * Map thread of the bot. One per bot AI.
 */

#ifndef MANGOS_COMPANIONS_REWARD_CHOICE_H
#define MANGOS_COMPANIONS_REWARD_CHOICE_H

#include "Life/Errands/RewardChooser.h"

namespace Companions
{
    class CompanionRewardChooser final : public Life::IRewardChooser
    {
    public:
        bool Choose(Player* me, Quest const* quest, uint32& index) override;
        void Done(Player* me, uint32 questId) override;

    private:
        bool AskPlayer(Player* me, Quest const* quest, uint32& index);

        uint32 m_requestId = 0;     // Life::Requests::Board
        uint32 m_questId = 0;       // the quest asked about
    };
}

#endif
