#include "Commander/Perception/Profiles/QuestNeeds.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"

namespace Commander
{
namespace QuestNeeds
{
    Cache::Cache(std::vector<Player*> members) : m_members(std::move(members))
    {
        for (Player* p : m_members)
        {
            // (the status map holds the log and the rewarded quests: the log's are the current ones)
            for (auto const& entry : p->GetQuestStatusMap())
            {
                if (entry.second.m_status != QUEST_STATUS_INCOMPLETE || !p->IsCurrentQuest(entry.first))
                    continue;
                Quest const* quest = sObjectMgr.GetQuestTemplate(entry.first);
                if (!quest)
                    continue;
                for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    int32 const target = quest->ReqCreatureOrGOId[i];
                    if (target > 0 && !quest->ReqSpell[i] &&
                        entry.second.m_creatureOrGOcount[i] < quest->ReqCreatureOrGOCount[i])
                        m_kills.insert(uint32(target));
                }
            }
        }
    }

    bool Cache::Needs(uint32 creatureEntry)
    {
        auto const known = m_byEntry.find(creatureEntry);
        if (known != m_byEntry.end())
            return known->second;

        bool needed = m_kills.count(creatureEntry) != 0;
        if (!needed)
        {
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(creatureEntry);
            if (info && info->loot_id)
                for (Player* p : m_members)
                    if (LootTemplates_Creature.HaveQuestLootForPlayer(info->loot_id, p))
                    {
                        needed = true;
                        break;
                    }
        }
        m_byEntry[creatureEntry] = needed;
        return needed;
    }
}
}
