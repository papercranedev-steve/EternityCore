#include "Shared/QuestGrant.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include "Log.h"
#include <algorithm>
#include <set>
#include <unordered_map>

namespace BotShared
{
    namespace
    {
        // quest id -> the quests whose NextQuestInChain it is
        std::unordered_map<uint32, std::vector<uint32>> const& ChainPredecessors()
        {
            static std::unordered_map<uint32, std::vector<uint32>> const predecessors = []
            {
                std::unordered_map<uint32, std::vector<uint32>> map;
                for (auto const& itr : sObjectMgr.GetQuestTemplates())
                    if (Quest const* quest = itr.second.get())
                        if (uint32 const next = quest->GetNextQuestInChain())
                            map[next].push_back(quest->GetQuestId());
                return map;
            }();
            return predecessors;
        }

        // The quest done right before `quest`, nullptr when it starts the chain.
        Quest const* Before(Quest const* quest, std::function<bool(Quest const*)> const& fits)
        {
            // A negative PrevQuestId only asks for that quest to be in the log at the same time.
            if (quest->GetPrevQuestId() > 0)
            {
                Quest const* prev = sObjectMgr.GetQuestTemplate(uint32(quest->GetPrevQuestId()));
                if (prev && fits(prev))
                    return prev;
            }
            auto const itr = ChainPredecessors().find(quest->GetQuestId());
            if (itr != ChainPredecessors().end())
                for (uint32 id : itr->second)
                {
                    Quest const* prev = sObjectMgr.GetQuestTemplate(id);
                    if (prev && fits(prev))
                        return prev;
                }
            return nullptr;
        }

        // Every quest done right before `quest` (PrevQuestId first, then the quests leading to it).
        std::vector<Quest const*> AllBefore(Quest const* quest, std::function<bool(Quest const*)> const& fits)
        {
            std::vector<Quest const*> before;
            auto consider = [&](uint32 id)
            {
                Quest const* prev = sObjectMgr.GetQuestTemplate(id);
                if (prev && fits(prev) && std::find(before.begin(), before.end(), prev) == before.end())
                    before.push_back(prev);
            };
            if (quest->GetPrevQuestId() > 0)
                consider(uint32(quest->GetPrevQuestId()));
            auto const itr = ChainPredecessors().find(quest->GetQuestId());
            if (itr != ChainPredecessors().end())
                for (uint32 id : itr->second)
                    consider(id);
            return before;
        }

        size_t const MAX_WAYS = 16;
        size_t const MAX_STEPS = 20;

        // Walks back from the newest step of `way` (newest first) and adds every finished way.
        void CollectWays(std::vector<Quest const*>& way, std::function<bool(Quest const*)> const& fits,
                         std::vector<std::vector<Quest const*>>& ways)
        {
            if (ways.size() >= MAX_WAYS)
                return;
            std::vector<Quest const*> before;
            if (way.size() < MAX_STEPS)
                for (Quest const* prev : AllBefore(way.back(), fits))
                    if (std::find(way.begin(), way.end(), prev) == way.end())
                        before.push_back(prev);
            if (before.empty())
            {
                ways.emplace_back(way.rbegin(), way.rend());
                return;
            }
            for (Quest const* prev : before)
            {
                way.push_back(prev);
                CollectWays(way, fits, ways);
                way.pop_back();
            }
        }

        void MarkRewarded(Player* player, Quest const* quest)
        {
            uint32 const id = quest->GetQuestId();
            if (quest->IsRepeatable() || player->GetQuestRewardStatus(id) || player->IsCurrentQuest(id))
                return;
            QuestStatusData& status = player->GetQuestStatusMap()[id];
            status.m_rewarded = true;
            player->SetQuestStatus(id, QUEST_STATUS_COMPLETE);   // marks the row changed (or keeps it new)
        }
    }

    namespace QuestGrant
    {
        std::vector<Quest const*> ChainUpTo(Quest const* quest, std::function<bool(Quest const*)> const& fits)
        {
            std::vector<Quest const*> chain;
            std::set<uint32> seen;
            for (Quest const* step = quest; step && seen.insert(step->GetQuestId()).second && chain.size() < 20;
                 step = Before(step, fits))
                chain.push_back(step);
            std::reverse(chain.begin(), chain.end());
            return chain;
        }

        std::vector<Quest const*> ChainUpTo(Player const* player, Quest const* quest)
        {
            if (!player || !quest)
                return {};
            return ChainUpTo(quest, [player](Quest const* q)
            {
                return player->SatisfyQuestClass(q, false) && player->SatisfyQuestRace(q, false);
            });
        }

        void MarkChainRewarded(Player* player, Quest const* quest)
        {
            if (!player || !quest)
                return;
            for (Quest const* step : ChainUpTo(player, quest))
                MarkRewarded(player, step);
        }

        std::vector<std::vector<Quest const*>> AllChainsUpTo(Quest const* quest, std::function<bool(Quest const*)> const& fits)
        {
            std::vector<std::vector<Quest const*>> ways;
            if (!quest)
                return ways;
            std::vector<Quest const*> way{ quest };
            CollectWays(way, fits, ways);
            return ways;
        }

        void MarkStepsRewarded(Player* player, std::vector<Quest const*> const& steps)
        {
            if (!player)
                return;
            for (Quest const* step : steps)
                if (step)
                    MarkRewarded(player, step);
        }

        void GiveRewards(Player* player, Quest const* quest)
        {
            if (!player || !quest)
                return;

            uint32 const castId = quest->GetRewSpellCast() ? quest->GetRewSpellCast() : quest->GetRewSpell();
            if (SpellEntry const* cast = castId ? sSpellMgr.GetSpellEntry(castId) : nullptr)
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (cast->Effect[i] == SPELL_EFFECT_LEARN_SPELL && cast->EffectTriggerSpell[i] &&
                        !player->HasSpell(cast->EffectTriggerSpell[i]))
                        player->LearnSpell(cast->EffectTriggerSpell[i], false);

            for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            {
                uint32 const itemId = quest->RewItemId[i];
                uint32 const count = quest->RewItemCount[i];
                if (!itemId || !count)
                    continue;
                ItemPosCountVec dest;
                if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) != EQUIP_ERR_OK)
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestGrant] %s: no room for item %u of quest %u.",
                        player->GetName(), itemId, quest->GetQuestId());
                    continue;
                }
                player->StoreNewItem(dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));
            }
        }

        void Complete(Player* player, Quest const* quest)
        {
            MarkChainRewarded(player, quest);
            GiveRewards(player, quest);
        }
    }
}
