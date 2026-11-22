#include "Life/Errands/QuestObjectRun.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/LootFacts.h"
#include "Shared/Inventory.h"
#include "Shared/ObjectUse.h"
#include "Player.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Timer.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <vector>

namespace Life
{
namespace QuestObjectRun
{
    namespace
    {
        // The bot's own quests want it to use this object.
        bool NeedsForQuest(Player* me, GameObject const* go)
        {
            if (!go->isSpawned())
                return false;
            if (me->HasQuestForGO(int32(go->GetEntry())))
                return true;
            GameObjectInfo const* info = go->GetGOInfo();
            switch (info->type)
            {
                case GAMEOBJECT_TYPE_CHEST:
                    // A quest item it still needs inside (or the chest's own quest).
                    return go->getLootState() == GO_READY && go->ActivateToQuest(me);
                case GAMEOBJECT_TYPE_GOOBER:
                    return info->goober.questId > 0 && me->GetQuestStatus(info->goober.questId) == QUEST_STATUS_INCOMPLETE;
                default:
                    return false;
            }
        }

        class QuestObjectCheck
        {
        public:
            QuestObjectCheck(Player* me, float range) : m_me(me), m_range(range) {}
            bool operator()(GameObject* go)
            {
                return m_me->IsWithinDist(go, m_range, false) && NeedsForQuest(m_me, go);
            }

        private:
            Player* m_me;
            float m_range;
        };

        // A point APPROACH_YD in front of the object on the bot's side.
        void ApproachPoint(Player const* me, GameObject const* go, float& x, float& y, float& z)
        {
            float const angle = go->GetAngle(me);
            x = go->GetPositionX() + APPROACH_YD * std::cos(angle);
            y = go->GetPositionY() + APPROACH_YD * std::sin(angle);
            z = go->GetPositionZ();
        }

        GameObject* Pick(Player* me, QuestObjectRunState& state, WorldObject const* anchor)
        {
            std::list<GameObject*> found;
            QuestObjectCheck check(me, SEARCH_YD);
            MaNGOS::GameObjectListSearcher<QuestObjectCheck> searcher(found, check);
            Cell::VisitGridObjects(me, searcher, SEARCH_YD);

            std::vector<GameObject*> objects;
            for (GameObject* go : found)
                if (!Errands::IsDone(state.done, go->GetObjectGuid()) && anchor->IsWithinDist(go, ANCHOR_YD, false) &&
                    BotShared::ObjectUse::HowToUse(me, go).way != BotShared::ObjectUse::Way::Cannot)
                    objects.push_back(go);
            std::sort(objects.begin(), objects.end(), [me](GameObject* a, GameObject* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (GameObject* go : objects)
            {
                float x, y, z;
                ApproachPoint(me, go, x, y, z);
                if (go->IsAtInteractDistance(me) || Errands::WalkIsSafe(me, x, y, z, MAX_PATH_DETOUR))
                    return go;
                Errands::MarkDone(state.done, go->GetObjectGuid(), DONE_KEEP);
            }
            return nullptr;
        }

        void Finish(Player* me, QuestObjectRunState& state)
        {
            Errands::MarkDone(state.done, state.object, DONE_KEEP);
            state.object.Clear();
            state.opening = false;
            state.bagsBefore.clear();
            Errands::StopWalk(me);
        }

        // The use or the cast is over: take what a loot window shows, put on upgrades.
        void TakeWhatOpened(Player* me, QuestObjectRunState& state)
        {
            if (me->GetLootGuid() == state.object)
                BotShared::LootFacts::TakeOpenLoot(me);
            BotShared::Inventory::WearUpgrades(me, state.bagsBefore);
        }
    }

    void Stop(Player* me, QuestObjectRunState& state)
    {
        if (state.object.IsEmpty())
            return;
        // A cast at the object goes on by itself; the window it may open is closed by the next
        // loot the bot opens (Player::SendLoot releases the previous one).
        state.object.Clear();
        state.opening = false;
        state.bagsBefore.clear();
        Errands::StopWalk(me);
    }

    bool Update(Player* me, QuestObjectRunState& state, Player const* anchor)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        // (an opening cast keeps the bot standing still: MayRun stays true while it casts)
        if (!Errands::MayRun(me, anchor))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        GameObject* go = nullptr;
        if (!state.object.IsEmpty())
        {
            go = me->GetMap()->GetGameObject(state.object);
            if (state.opening)
            {
                if (me->IsNonMeleeSpellCasted(false))
                    return true;    // Opening, Pick Lock: the object opens when the cast ends
                TakeWhatOpened(me, state);
                Finish(me, state);
                return true;
            }
            if (!go || !NeedsForQuest(me, go) || WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS)
            {
                Finish(me, state);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            go = Pick(me, state, center);
            if (!go)
            {
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            state.object = go->GetObjectGuid();
            state.startedMs = now;
            state.nextMoveMs = 0;
        }

        // Walks up to it like a player, not just into reach (nextMoveMs: it was sent towards it).
        if (go->IsAtInteractDistance(me) && Errands::CloseEnoughToUse(me, go, APPROACH_YD, state.nextMoveMs != 0))
        {
            if (!me->IsStopped())
                me->StopMoving();
            BotShared::ObjectUse::Plan const plan = BotShared::ObjectUse::HowToUse(me, go);
            BotShared::Inventory::CollectBagItemGuids(me, state.bagsBefore);
            if (!BotShared::ObjectUse::Start(me, go, plan))
            {
                Finish(me, state);
                return false;
            }
            me->SetFacingToObject(go);
            state.opening = true;
            // A use or a loot is over at once; a cast is waited for on the next ticks.
            if (!me->IsNonMeleeSpellCasted(false))
            {
                TakeWhatOpened(me, state);
                Finish(me, state);
            }
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            float x, y, z;
            ApproachPoint(me, go, x, y, z);
            me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
