#include "Companions/RewardChoice.h"
#include "Companions/CompanionMgr.h"
#include "Shared/QuestRewards.h"
#include "Player.h"

namespace Companions
{
    bool CompanionRewardChooser::Choose(Player* me, Quest const* quest, uint32& index)
    {
        switch (sCompanionMgr.GetRewardMode(me->GetGUIDLow()))
        {
            case RewardMode::AskPlayer:
                return AskPlayer(me, quest, index);
            case RewardMode::Auto:
            default:
                index = BotShared::QuestRewards::BestChoice(me, quest);
                return true;
        }
    }

    bool CompanionRewardChooser::AskPlayer(Player* me, Quest const* quest, uint32& index)
    {
        // Stage S5 (request + addon window) asks the player here. Not built yet: pick itself.
        index = BotShared::QuestRewards::BestChoice(me, quest);
        return true;
    }

    void CompanionRewardChooser::Done(Player* /*me*/, uint32 /*questId*/)
    {
        // Stage S5: close the open request for this quest.
    }
}
