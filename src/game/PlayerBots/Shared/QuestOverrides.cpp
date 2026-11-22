#include "Shared/QuestOverrides.h"
#include "Player.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include <algorithm>
#include <memory>
#include <unordered_set>

namespace BotShared
{
namespace QuestOverrides
{
    namespace
    {
        // Filled once at startup, read only afterwards (map threads).
        std::unordered_set<uint32> g_followCredit;
        std::unordered_set<uint32> g_noItemUse;

        // Raises `bot`'s counts of `quest` to `leader`'s.
        void FollowCounts(Player* bot, Player* leader, Quest const* quest)
        {
            uint32 const questId = quest->GetQuestId();
            QuestStatusMap& botMap = bot->GetQuestStatusMap();
            QuestStatusMap& leaderMap = leader->GetQuestStatusMap();
            auto const botItr = botMap.find(questId);
            auto const leaderItr = leaderMap.find(questId);
            if (botItr == botMap.end() || leaderItr == leaderMap.end() || botItr->second.m_status != QUEST_STATUS_INCOMPLETE)
                return;

            QuestStatusData& mine = botItr->second;
            QuestStatusData const& his = leaderItr->second;
            bool changed = false;
            for (uint32 j = 0; j < QUEST_OBJECTIVES_COUNT; ++j)
            {
                if (!quest->ReqCreatureOrGOId[j])
                    continue;
                uint32 const target = std::min(his.m_creatureOrGOcount[j], quest->ReqCreatureOrGOCount[j]);
                if (mine.m_creatureOrGOcount[j] >= target)
                    continue;
                mine.m_creatureOrGOcount[j] = target;
                changed = true;
                bot->SendQuestUpdateAddCreatureOrGo(quest, ObjectGuid(), j, target);
            }
            if (!changed)
                return;
            if (mine.uState != QUEST_NEW)
                mine.uState = QUEST_CHANGED;
            if (bot->CanCompleteQuest(questId))
                bot->CompleteQuest(questId);
        }
    }

    void Load()
    {
        g_followCredit.clear();
        g_noItemUse.clear();
        std::unique_ptr<QueryResult> exists(WorldDatabase.Query("SHOW TABLES LIKE 'custom_bot_quest'"));
        if (!exists)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [QuestOverrides] No table `custom_bot_quest`: no exceptions.");
            return;
        }
        std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `quest`, `follow_credit`, `no_item_use` FROM `custom_bot_quest`"));
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                uint32 const questId = f[0].GetUInt32();
                if (f[1].GetUInt8())
                    g_followCredit.insert(questId);
                if (f[2].GetUInt8())
                    g_noItemUse.insert(questId);
            } while (result->NextRow());
        }
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [QuestOverrides] %u quests follow the player's credit, %u leave their items to him.",
            uint32(g_followCredit.size()), uint32(g_noItemUse.size()));
    }

    bool FollowsCredit(uint32 questId)
    {
        return g_followCredit.count(questId) != 0;
    }

    bool NoItemUse(uint32 questId)
    {
        return g_noItemUse.count(questId) != 0;
    }

    void OnCredit(Player* player)
    {
        if (g_followCredit.empty() || !player || player->IsBot())
            return;
        Group* group = player->GetGroup();
        if (!group)
            return;

        for (auto const& entry : player->GetQuestStatusMap())
        {
            uint32 const questId = entry.first;
            // in his log: taken, not handed in
            if (!FollowsCredit(questId) || entry.second.m_rewarded ||
                (entry.second.m_status != QUEST_STATUS_INCOMPLETE && entry.second.m_status != QUEST_STATUS_COMPLETE))
                continue;
            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest)
                continue;
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* member = itr->getSource();
                if (!member || member == player || !member->IsBot() || !member->IsInWorld() ||
                    member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || !member->IsAtGroupRewardDistance(player))
                    continue;
                FollowCounts(member, player, quest);
            }
        }
    }
}
}
