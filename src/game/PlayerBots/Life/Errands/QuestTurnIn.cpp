#include "Life/Errands/QuestTurnIn.h"
#include "Life/Errands/RewardChooser.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/Inventory.h"
#include "Shared/QuestRewards.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "WorldSession.h"
#include "Timer.h"
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

        // The first finished quest `giver` ends, 0 = none.
        uint32 EndedBy(WorldObject const* giver, std::vector<uint32> const& quests)
        {
            for (uint32 questId : quests)
                if (giver->HasInvolvedQuest(questId))
                    return questId;
            return 0;
        }

        class GiverCheck
        {
        public:
            GiverCheck(Player* me, float range, std::vector<uint32> const& quests) : m_me(me), m_range(range), m_quests(quests) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && c->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) && !c->IsHostileTo(m_me) &&
                       m_me->IsWithinDist(c, m_range, false) && EndedBy(c, m_quests);
            }
            bool operator()(GameObject* go)
            {
                return go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER &&
                       m_me->IsWithinDist(go, m_range, false) && EndedBy(go, m_quests);
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

        bool InReach(Player* me, WorldObject* giver)
        {
            if (GameObject* go = giver->ToGameObject())
                return go->IsAtInteractDistance(me);
            return me->IsWithinDistInMap(giver, INTERACTION_DISTANCE);
        }

        WorldObject* Pick(Player* me, QuestTurnInState& state, WorldObject const* anchor, uint32& questId)
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
                return Errands::IsDone(state.done, g->GetObjectGuid()) || !anchor->IsWithinDist(g, ANCHOR_YD, false);
            }), givers.end());
            std::sort(givers.begin(), givers.end(), [me](WorldObject* a, WorldObject* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (WorldObject* g : givers)
            {
                if (InReach(me, g) || Errands::WalkIsSafe(me, g->GetPositionX(), g->GetPositionY(), g->GetPositionZ(), MAX_PATH_DETOUR))
                {
                    questId = EndedBy(g, quests);
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
        bool TurnIn(Player* me, WorldObject* giver, Quest const* quest, uint32 reward)
        {
            std::set<ObjectGuid> before;
            BotShared::Inventory::CollectBagItemGuids(me, before);

            WorldPackets::Quest::QuestgiverChooseReward packet;
            packet.guid = giver->GetObjectGuid();
            packet.quest = quest->GetQuestId();
            packet.reward = reward;
            me->GetSession()->HandleQuestgiverChooseRewardOpcode(packet);

            BotShared::Inventory::WearUpgrades(me, before);
            return me->GetQuestRewardStatus(quest->GetQuestId()) || me->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_COMPLETE;
        }
    }

    void Stop(Player* me, QuestTurnInState& state, IRewardChooser& chooser)
    {
        if (state.giver.IsEmpty())
            return;
        Clear(me, state, chooser);
        Errands::StopWalk(me);
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
        WorldObject* giver = nullptr;
        Quest const* quest = nullptr;
        if (!state.giver.IsEmpty())
        {
            giver = FindGiver(me, state.giver);
            quest = sObjectMgr.GetQuestTemplate(state.questId);
            bool const waitingForChoice = state.choiceSinceMs != 0;
            if (!giver || !quest || me->GetQuestStatus(state.questId) != QUEST_STATUS_COMPLETE ||
                (!waitingForChoice && WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS))
            {
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
            giver = Pick(me, state, center, questId);
            quest = giver ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
            if (!quest)
            {
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            state.giver = giver->GetObjectGuid();
            state.questId = questId;
            state.startedMs = now;
            state.nextMoveMs = 0;
            state.choiceSinceMs = 0;
        }

        if (InReach(me, giver))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->SetFacingToObject(giver);

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

            if (!TurnIn(me, giver, quest, reward))
                Errands::MarkDone(state.done, giver->GetObjectGuid(), DONE_KEEP);
            Clear(me, state, chooser);
            state.nextSearchMs = 0;     // the giver may end another of its quests
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, giver->GetPositionX(), giver->GetPositionY(), giver->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
