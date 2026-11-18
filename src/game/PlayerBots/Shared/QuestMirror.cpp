#include "Shared/QuestMirror.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include <algorithm>

namespace BotShared
{
    namespace QuestMirror
    {
        ShowResult Show(Player* player, uint32 mirrorId)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(mirrorId);
            if (!player || !quest)
                return ShowResult::NoQuest;
            if (player->IsCurrentQuest(mirrorId))
                return ShowResult::AlreadyShown;
            if (!player->SatisfyQuestLog(false))
                return ShowResult::LogFull;
            player->AddQuest(quest, nullptr);
            return ShowResult::Shown;
        }

        bool SetCount(Player* player, uint32 mirrorId, uint32 objective, uint32 count)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(mirrorId);
            if (!player || !quest || objective >= QUEST_OBJECTIVES_COUNT || !player->IsCurrentQuest(mirrorId))
                return false;
            uint32 const needed = quest->ReqCreatureOrGOCount[objective];
            if (!quest->ReqCreatureOrGOId[objective] || !needed)
                return false;

            count = std::min(count, needed);
            QuestStatusData& status = player->GetQuestStatusMap()[mirrorId];
            if (status.m_creatureOrGOcount[objective] == count)
                return true;
            status.m_creatureOrGOcount[objective] = count;
            if (status.uState != QUEST_NEW)
                status.uState = QUEST_CHANGED;
            player->SendQuestUpdateAddCreatureOrGo(quest, ObjectGuid(), objective, count);

            if (status.m_status == QUEST_STATUS_INCOMPLETE && player->CanCompleteQuest(mirrorId))
                player->CompleteQuest(mirrorId);
            else if (status.m_status == QUEST_STATUS_COMPLETE && count < needed)
                player->IncompleteQuest(mirrorId);
            return true;
        }

        bool Remove(Player* player, uint32 mirrorId)
        {
            if (!player)
                return false;
            uint16 const slot = player->FindQuestSlot(mirrorId);
            if (slot >= MAX_QUEST_LOG_SIZE)
                return false;
            player->RemoveQuestAtSlot(slot);
            return true;
        }
    }
}
