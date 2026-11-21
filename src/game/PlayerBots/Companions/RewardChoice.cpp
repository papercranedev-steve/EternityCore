#include "Companions/RewardChoice.h"
#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/Gates.h"
#include "Shared/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/QuestRewards.h"
#include "Player.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "QuestDef.h"
#include <cstdlib>

namespace Companions
{
    namespace
    {
        // The player leading the companion's group, or null (a bot leads, no group).
        Player* HumanLeader(Player* me)
        {
            Group* group = me->GetGroup();
            if (!group)
                return nullptr;
            Player* leader = sObjectAccessor.FindPlayer(group->GetLeaderGuid());
            return leader && !leader->IsBot() ? leader : nullptr;
        }
    }

    bool CompanionRewardChooser::Choose(Player* me, Quest const* quest, uint32& index)
    {
        // The player picks once its affinity allows it (Affinity/Gates PickRewards); the
        // reward mode AskPlayer (.partybot rewardmode) forces it earlier, for tests.
        uint32 const guid = me->GetGUIDLow();
        bool const ask = sCompanionMgr.GetRewardMode(guid) == RewardMode::AskPlayer ||
                         (sCompanionMgr.IsCompanion(guid) && Gates::Allows(guid, Gates::Gate::PickRewards));
        if (ask)
            return AskPlayer(me, quest, index);
        index = BotShared::QuestRewards::BestChoice(me, quest);
        return true;
    }

    bool CompanionRewardChooser::AskPlayer(Player* me, Quest const* quest, uint32& index)
    {
        uint32 const best = BotShared::QuestRewards::BestChoice(me, quest);

        if (!m_requestId || m_questId != quest->GetQuestId())
        {
            if (m_requestId)
                sRequestBoard.Close(m_requestId, Life::Requests::Outcome::Cancelled);
            m_requestId = 0;
            m_questId = quest->GetQuestId();

            Player* leader = HumanLeader(me);
            if (!leader)
            {
                index = best;
                return true;
            }

            Life::Requests::Ask ask;
            ask.intent = Life::Requests::Intent::RewardChoice;
            for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            {
                uint32 const itemId = quest->RewChoiceItemId[i];
                if (!itemId)
                    continue;
                // (the addon shows the item in the client's language: "item:<id>[:<count>]")
                std::string label = "item:" + std::to_string(itemId);
                if (quest->RewChoiceItemCount[i] > 1)
                    label += ":" + std::to_string(quest->RewChoiceItemCount[i]);
                ask.options.push_back({ std::to_string(i), label });
            }
            ask.fallback = std::to_string(best);

            CompanionInfo const* info = sCompanionMgr.Find(me->GetGUIDLow());
            std::vector<std::string> const noTags;
            ask.text = BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "reward.ask", info ? info->tags : noTags, { { "quest", quest->GetTitle() } });

            m_requestId = sRequestBoard.Open(me, leader, ask);
            if (!m_requestId)
            {
                index = best;
                return true;
            }
            BotShared::BotChat::SayToParty(me, ask.text);
            return false;
        }

        Life::Requests::Status const status = sRequestBoard.Poll(m_requestId);
        if (status.state == Life::Requests::State::Open)
            return false;

        index = best;
        if (status.state == Life::Requests::State::Answered || status.state == Life::Requests::State::Expired)
        {
            uint32 const chosen = uint32(std::strtoul(status.answer.c_str(), nullptr, 10));
            if (chosen < QUEST_REWARD_CHOICES_COUNT && quest->RewChoiceItemId[chosen])
                index = chosen;
        }
        sRequestBoard.Close(m_requestId, status.state == Life::Requests::State::Answered ?
                            Life::Requests::Outcome::Accepted : Life::Requests::Outcome::Expired);
        m_requestId = 0;
        return true;
    }

    void CompanionRewardChooser::Done(Player* /*me*/, uint32 questId)
    {
        if (m_requestId && m_questId == questId)
        {
            sRequestBoard.Close(m_requestId, Life::Requests::Outcome::Cancelled);
            m_requestId = 0;
        }
        m_questId = 0;
    }
}
