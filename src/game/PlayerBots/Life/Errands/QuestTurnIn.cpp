#include "Life/Errands/QuestTurnIn.h"
#include "Life/Errands/RewardChooser.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/Inventory.h"
#include "Shared/QuestRewards.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "WorldSession.h"
#include "Timer.h"
#include "Log.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <list>
#include <set>
#include <vector>

namespace Life
{
    bool AutoRewardChooser::Choose(Player* me, Quest const* quest, uint32& index)
    {
        index = BotShared::QuestRewards::BestChoice(me, quest);
        return true;
    }

namespace QuestTurnIn
{
    namespace
    {
        // Quests in the log that are complete and may be rewarded now.
        std::vector<uint32> FinishedQuests(Player* me)
        {
            std::vector<uint32> quests;
            // The status map (public) holds every quest in the log; COMPLETE ones are not rewarded yet.
            for (auto const& entry : me->GetQuestStatusMap())
            {
                uint32 const questId = entry.first;
                if (entry.second.m_status != QUEST_STATUS_COMPLETE)
                    continue;
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest && me->CanRewardQuest(quest, false))
                    quests.push_back(questId);
            }
            return quests;
        }

        // The leader has (or had) the quest too: it is handed in with him.
        bool WithLeader(Player const* anchor, uint32 questId)
        {
            return anchor && anchor->GetQuestStatus(questId) != QUEST_STATUS_NONE;
        }

        // The first finished quest `giver` ends where the party stands (GiverVisit::LeaderBy for
        // the leader's quests, ANCHOR_YD of the center for the bot's own), 0 = none.
        uint32 EndedBy(WorldObject const* giver, std::vector<uint32> const& quests, Player const* anchor,
                       WorldObject const* center)
        {
            for (uint32 questId : quests)
            {
                if (!giver->HasInvolvedQuest(questId))
                    continue;
                if (WithLeader(anchor, questId) ? GiverVisit::LeaderBy(anchor, giver) : center->IsWithinDist(giver, ANCHOR_YD, false))
                    return questId;
            }
            return 0;
        }

        bool EndsAny(WorldObject const* giver, std::vector<uint32> const& quests)
        {
            for (uint32 questId : quests)
                if (giver->HasInvolvedQuest(questId))
                    return true;
            return false;
        }

        class GiverCheck
        {
        public:
            GiverCheck(Player* me, float range, std::vector<uint32> const& quests) : m_me(me), m_range(range), m_quests(quests) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && c->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) && !c->IsHostileTo(m_me) &&
                       m_me->IsWithinDist(c, m_range, false) && EndsAny(c, m_quests);
            }
            bool operator()(GameObject* go)
            {
                return go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER &&
                       m_me->IsWithinDist(go, m_range, false) && EndsAny(go, m_quests);
            }

        private:
            Player* m_me;
            float m_range;
            std::vector<uint32> const& m_quests;
        };

        WorldObject* FindGiver(Player* me, ObjectGuid guid)
        {
            if (guid.IsGameObject())
                return me->GetMap()->GetGameObject(guid);
            return me->GetMap()->GetCreature(guid);
        }

        WorldObject* Pick(Player* me, QuestTurnInState& state, Player const* anchor, WorldObject const* center, uint32& questId)
        {
            std::vector<uint32> const quests = FinishedQuests(me);
            if (quests.empty())
                return nullptr;

            GiverCheck check(me, SEARCH_YD, quests);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<GiverCheck> creatureSearcher(creatures, check);
            Cell::VisitGridObjects(me, creatureSearcher, SEARCH_YD);
            std::list<GameObject*> objects;
            MaNGOS::GameObjectListSearcher<GiverCheck> objectSearcher(objects, check);
            Cell::VisitGridObjects(me, objectSearcher, SEARCH_YD);

            std::vector<WorldObject*> givers(creatures.begin(), creatures.end());
            givers.insert(givers.end(), objects.begin(), objects.end());
            givers.erase(std::remove_if(givers.begin(), givers.end(), [&](WorldObject* g)
            {
                return Errands::IsDone(state.done, g->GetObjectGuid(), RETRY_MS) || !EndedBy(g, quests, anchor, center);
            }), givers.end());
            std::sort(givers.begin(), givers.end(), [me](WorldObject* a, WorldObject* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (WorldObject* g : givers)
            {
                if (GiverVisit::InReach(me, g) ||
                    Errands::WalkIsSafe(me, g->GetPositionX(), g->GetPositionY(), g->GetPositionZ(), MAX_PATH_DETOUR))
                {
                    questId = EndedBy(g, quests, anchor, center);
                    return g;
                }
                Errands::MarkDone(state.done, g->GetObjectGuid(), DONE_KEEP);
            }
            return nullptr;
        }

        void Clear(Player* me, QuestTurnInState& state, IRewardChooser& chooser)
        {
            if (state.questId)
                chooser.Done(me, state.questId);
            state.giver.Clear();
            state.questId = 0;
            state.choiceSinceMs = 0;
        }

        // Rewards the quest at the giver through the client's handler. False: refused (bags full).
        bool TurnIn(Player* me, WorldObject* giver, Quest const* quest, uint32 reward, TurnedInQuest& done)
        {
            std::set<ObjectGuid> before;
            BotShared::Inventory::CollectBagItemGuids(me, before);

            WorldPackets::Quest::QuestgiverChooseReward packet;
            packet.guid = giver->GetObjectGuid();
            packet.quest = quest->GetQuestId();
            packet.reward = reward;
            me->GetSession()->HandleQuestgiverChooseRewardOpcode(packet);

            bool const rewarded = me->GetQuestRewardStatus(quest->GetQuestId()) ||
                                  me->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_COMPLETE;
            done.wore = BotShared::Inventory::WearUpgrades(me, before) > 0;
            done.questId = quest->GetQuestId();
            done.giver = giver->GetObjectGuid();
            if (quest->GetRewChoiceItemsCount() > 0 && reward < QUEST_REWARD_CHOICES_COUNT)
                done.itemId = quest->RewChoiceItemId[reward];
            else if (quest->GetRewItemsCount() > 0)
                done.itemId = quest->RewItemId[0];
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestTurnIn] %s: quest %u at %s %s.", me->GetName(), quest->GetQuestId(),
                giver->GetObjectGuid().GetString().c_str(), rewarded ? "rewarded" : "refused");
            return rewarded;
        }
    }

    void Stop(Player* me, QuestTurnInState& state, IRewardChooser& chooser)
    {
        bool const visiting = !state.giver.IsEmpty() || state.visit.lingerUntilMs;
        GiverVisit::End(state.visit);
        if (!visiting)
            return;
        Clear(me, state, chooser);
        Errands::StopWalk(me);
    }

    std::vector<TurnedInQuest> TakeTurnedIn(QuestTurnInState& state)
    {
        std::vector<TurnedInQuest> out;
        out.swap(state.turnedIn);
        return out;
    }

    bool Update(Player* me, QuestTurnInState& state, Player const* anchor, IRewardChooser& chooser)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (!Errands::MayRun(me, anchor))
        {
            Stop(me, state, chooser);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        if (GiverVisit::Lingering(me, state.visit, now))
            return true;

        WorldObject* giver = nullptr;
        Quest const* quest = nullptr;
        if (!state.giver.IsEmpty())
        {
            giver = FindGiver(me, state.giver);
            quest = sObjectMgr.GetQuestTemplate(state.questId);
            bool const waitingForChoice = state.choiceSinceMs != 0;
            // The leader walked away from the giver of a quest he has too: the bot follows him.
            bool const leaderLeft = giver && WithLeader(anchor, state.questId) && !GiverVisit::LeaderBy(anchor, giver);
            if (!giver || !quest || me->GetQuestStatus(state.questId) != QUEST_STATUS_COMPLETE || leaderLeft ||
                (!waitingForChoice && WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS))
            {
                if (!leaderLeft)
                    Errands::MarkDone(state.done, state.giver, DONE_KEEP);
                Stop(me, state, chooser);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            uint32 questId = 0;
            giver = Pick(me, state, anchor, center, questId);
            quest = giver ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
            if (!quest)
            {
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            state.giver = giver->GetObjectGuid();
            state.questId = questId;
            state.startedMs = now;
            state.choiceSinceMs = 0;
        }

        if (GiverVisit::Approach(me, state.visit, giver, anchor, now) != GiverVisit::Step::Act)
            return true;

        uint32 reward = 0;
        if (quest->GetRewChoiceItemsCount() > 1)
        {
            if (!state.choiceSinceMs)
                state.choiceSinceMs = now;
            if (!chooser.Choose(me, quest, reward))
            {
                // Nobody answered in time: the bot picks itself.
                if (WorldTimer::getMSTimeDiff(state.choiceSinceMs, now) < CHOICE_WAIT_MS)
                    return true;
                reward = BotShared::QuestRewards::BestChoice(me, quest);
            }
        }

        TurnedInQuest done;
        if (TurnIn(me, giver, quest, reward, done))
        {
            state.turnedIn.push_back(done);
            if (state.turnedIn.size() > TURNED_IN_KEEP)
                state.turnedIn.erase(state.turnedIn.begin());
        }
        else
            Errands::MarkDone(state.done, giver->GetObjectGuid(), DONE_KEEP);
        GiverVisit::Acted(state.visit, now);
        Clear(me, state, chooser);
        state.nextSearchMs = 0;     // the giver may end another of its quests
        return true;
    }
}
}
