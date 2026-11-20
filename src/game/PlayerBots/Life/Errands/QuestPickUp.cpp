#include "Life/Errands/QuestPickUp.h"
#include "Life/Errands/ErrandCommon.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
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

namespace Life
{
namespace QuestPickUp
{
    namespace
    {
        // The quests of `wanted` the bot may take now.
        std::vector<uint32> Takeable(Player* me, std::vector<uint32> const& wanted)
        {
            std::vector<uint32> quests;
            for (uint32 questId : wanted)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest && !me->IsCurrentQuest(questId) && me->CanTakeQuest(quest, false) &&
                    me->SatisfyQuestLog(false) && me->CanAddQuest(quest, false))
                    quests.push_back(questId);
            }
            return quests;
        }

        void GiveUp(Player* me, QuestPickUpState& state, ObjectGuid giver, char const* why)
        {
            Errands::MarkDone(state.done, giver, DONE_KEEP);
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestPickUp] %s: giver %s skipped for %u s (%s).",
                me->GetName(), giver.GetString().c_str(), RETRY_MS / 1000, why);
        }

        uint32 GivenBy(WorldObject const* giver, std::vector<uint32> const& quests)
        {
            for (uint32 questId : quests)
                if (giver->HasQuest(questId))
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
                       m_me->IsWithinDist(c, m_range, false) && GivenBy(c, m_quests);
            }
            bool operator()(GameObject* go)
            {
                return go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER &&
                       m_me->IsWithinDist(go, m_range, false) && GivenBy(go, m_quests);
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

        WorldObject* Pick(Player* me, QuestPickUpState& state, WorldObject const* anchor, std::vector<uint32> const& quests, uint32& questId)
        {
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
                return Errands::IsDone(state.done, g->GetObjectGuid(), RETRY_MS) || !anchor->IsWithinDist(g, ANCHOR_YD, false);
            }), givers.end());
            std::sort(givers.begin(), givers.end(), [me](WorldObject* a, WorldObject* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (WorldObject* g : givers)
            {
                if (InReach(me, g) || Errands::WalkIsSafe(me, g->GetPositionX(), g->GetPositionY(), g->GetPositionZ(), MAX_PATH_DETOUR))
                {
                    questId = GivenBy(g, quests);
                    return g;
                }
                GiveUp(me, state, g->GetObjectGuid(), "no safe walk");
            }
            return nullptr;
        }

        void Clear(QuestPickUpState& state)
        {
            state.giver.Clear();
            state.questId = 0;
        }
    }

    void Stop(Player* me, QuestPickUpState& state)
    {
        if (state.giver.IsEmpty())
            return;
        Clear(state);
        Errands::StopWalk(me);
    }

    bool Update(Player* me, QuestPickUpState& state, Player const* anchor, std::vector<uint32> const& wanted)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (wanted.empty() || !Errands::MayRun(me, anchor))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        WorldObject* giver = nullptr;
        if (!state.giver.IsEmpty())
        {
            giver = FindGiver(me, state.giver);
            if (!giver || me->IsCurrentQuest(state.questId) ||
                std::find(wanted.begin(), wanted.end(), state.questId) == wanted.end() ||
                WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS)
            {
                if (!me->IsCurrentQuest(state.questId))
                    GiveUp(me, state, state.giver, giver ? "not reached in time" : "giver gone");
                Stop(me, state);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            state.nextSearchMs = now + SEARCH_MS;
            std::vector<uint32> const quests = Takeable(me, wanted);
            if (quests.empty())
                return false;
            uint32 questId = 0;
            giver = Pick(me, state, center, quests, questId);
            if (!giver || !questId)
                return false;
            state.giver = giver->GetObjectGuid();
            state.questId = questId;
            state.startedMs = now;
            state.nextMoveMs = 0;
        }

        if (InReach(me, giver))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->SetFacingToObject(giver);

            WorldPackets::Quest::QuestgiverAcceptQuest packet;
            packet.guid = giver->GetObjectGuid();
            packet.quest = state.questId;
            me->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);

            if (!me->IsCurrentQuest(state.questId))
                GiveUp(me, state, giver->GetObjectGuid(), "accept refused");
            Clear(state);
            state.nextSearchMs = 0;
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
