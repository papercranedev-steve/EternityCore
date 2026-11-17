/*
 * Companions: who picks a companion's quest reward (Life/Errands/RewardChooser.h).
 *
 *  - RewardMode::Auto (default): the companion picks (Shared/QuestRewards::BestChoice).
 *  - RewardMode::AskPlayer: the player picks. Planned to be unlocked by affinity; the asking
 *    itself needs the request system and the addon window (stage S5). Until then this mode
 *    falls back to the companion's own pick, so a companion set to it never waits for an
 *    answer nobody can give. AskPlayer is the place S5 fills in: return false while the
 *    request is open (the turn-in waits up to QuestTurnIn::CHOICE_WAIT_MS), true with the
 *    player's answer, and drop the request in Done.
 *
 * Map thread of the bot. Stateless for now (one per bot AI).
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
    };
}

#endif
