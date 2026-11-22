#include "Life/Errands/QuestPickUp.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/BotChat.h"
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

namespace Life
{
namespace QuestPickUp
{
    namespace
    {
        // The quests of `wanted` the bot may take now.
        std::vector<WantedQuest> Takeable(Player* me, std::vector<WantedQuest> const& wanted)
        {
            std::vector<WantedQuest> quests;
            if (!me->SatisfyQuestLog(false))
                return quests;
            for (WantedQuest const& w : wanted)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(w.questId);
                if (quest && !me->IsCurrentQuest(w.questId) && me->CanTakeQuest(quest, false) && me->CanAddQuest(quest, false))
                    quests.push_back(w);
            }
            return quests;
        }

        void GiveUp(Player* me, QuestPickUpState& state, ObjectGuid giver, char const* why)
        {
            Errands::MarkDone(state.done, giver, DONE_KEEP);
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestPickUp] %s: giver %s skipped for %u s (%s).",
                me->GetName(), giver.GetString().c_str(), RETRY_MS / 1000, why);
        }

        // The first wanted quest `giver` gives where the party stands: the leader's at a giver he
        // stands by, the bot's own near the anchor. Null: none.
        WantedQuest const* GivenBy(WorldObject const* giver, std::vector<WantedQuest> const& quests, Player const* anchor,
                                   WorldObject const* center)
        {
            for (WantedQuest const& w : quests)
            {
                if (!giver->HasQuest(w.questId))
                    continue;
                if (w.withLeader ? GiverVisit::LeaderBy(anchor, giver) : center->IsWithinDist(giver, ANCHOR_YD, false))
                    return &w;
            }
            return nullptr;
        }

        class GiverCheck
        {
        public:
            GiverCheck(Player* me, float range, std::vector<WantedQuest> const& quests) : m_me(me), m_range(range), m_quests(quests) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && c->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) && !c->IsHostileTo(m_me) &&
                       m_me->IsWithinDist(c, m_range, false) && Gives(c);
            }
            bool operator()(GameObject* go)
            {
                return go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER &&
                       m_me->IsWithinDist(go, m_range, false) && Gives(go);
            }

        private:
            bool Gives(WorldObject const* giver) const
            {
                for (WantedQuest const& w : m_quests)
                    if (giver->HasQuest(w.questId))
                        return true;
                return false;
            }

            Player* m_me;
            float m_range;
            std::vector<WantedQuest> const& m_quests;
        };

        WorldObject* FindGiver(Player* me, ObjectGuid guid)
        {
            if (guid.IsGameObject())
                return me->GetMap()->GetGameObject(guid);
            return me->GetMap()->GetCreature(guid);
        }

        WorldObject* Pick(Player* me, QuestPickUpState& state, Player const* anchor, WorldObject const* center,
                          std::vector<WantedQuest> const& quests, WantedQuest& picked)
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
                return Errands::IsDone(state.done, g->GetObjectGuid(), RETRY_MS) || !GivenBy(g, quests, anchor, center);
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
                    picked = *GivenBy(g, quests, anchor, center);
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
            state.withLeader = false;
        }

        // The leader's quest, as the bot takes it when it follows his accept
        // (PlayerBotMgr::OnPlayerAcceptedQuest): no giver, so its accept scripts do not run again.
        void TakeWithLeader(Player* me, Quest const* quest, Player const* anchor)
        {
            me->AddQuest(quest, nullptr);
            if (me->CanCompleteQuest(quest->GetQuestId()))
                me->CompleteQuest(quest->GetQuestId());
            if (!anchor)
                return;
            if (Player* leader = me->GetMap()->GetPlayer(anchor->GetObjectGuid()))
                BotShared::BotChat::SystemTo(leader, "PBQUEST;" + std::string(me->GetName()) + ";accept;" +
                                             std::to_string(quest->GetQuestId()) + ";" + quest->GetTitle());
        }
    }

    void AddLeaderQuests(Player const* me, Player* leader, std::vector<WantedQuest>& wanted)
    {
        if (!leader || leader == me)
            return;
        // (the status map holds the log and the rewarded quests: the log's are the current ones)
        for (auto const& entry : leader->GetQuestStatusMap())
        {
            uint32 const questId = entry.first;
            if (!leader->IsCurrentQuest(questId) || me->GetQuestStatus(questId) != QUEST_STATUS_NONE)
                continue;
            if (std::any_of(wanted.begin(), wanted.end(), [questId](WantedQuest const& w) { return w.questId == questId; }))
                continue;
            WantedQuest w;
            w.questId = questId;
            w.withLeader = true;
            wanted.push_back(w);
        }
    }

    void Stop(Player* me, QuestPickUpState& state)
    {
        bool const visiting = !state.giver.IsEmpty() || state.visit.lingerUntilMs;
        GiverVisit::End(state.visit);
        if (!visiting)
            return;
        Clear(state);
        Errands::StopWalk(me);
    }

    bool Update(Player* me, QuestPickUpState& state, Player const* anchor, std::vector<WantedQuest> const& wanted)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (wanted.empty() && !state.visit.lingerUntilMs)
        {
            Stop(me, state);
            return false;
        }
        if (!Errands::MayRun(me, anchor))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        if (GiverVisit::Lingering(me, state.visit, now))
            return true;

        WorldObject* giver = nullptr;
        if (!state.giver.IsEmpty())
        {
            giver = FindGiver(me, state.giver);
            bool const stillWanted = std::any_of(wanted.begin(), wanted.end(), [&state](WantedQuest const& w)
            {
                return w.questId == state.questId;
            });
            // The leader walked away from the giver: the bot follows him (it comes back with him).
            bool const leaderLeft = state.withLeader && giver && !GiverVisit::LeaderBy(anchor, giver);
            if (!giver || me->IsCurrentQuest(state.questId) || !stillWanted || leaderLeft ||
                WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS)
            {
                if (!me->IsCurrentQuest(state.questId) && !leaderLeft)
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
            std::vector<WantedQuest> const quests = Takeable(me, wanted);
            if (quests.empty())
                return false;
            WantedQuest picked;
            giver = Pick(me, state, anchor, center, quests, picked);
            if (!giver || !picked.questId)
                return false;
            state.giver = giver->GetObjectGuid();
            state.questId = picked.questId;
            state.withLeader = picked.withLeader;
            state.startedMs = now;
        }

        GiverVisit::Step const step = GiverVisit::Approach(me, state.visit, giver, anchor, now);
        if (step != GiverVisit::Step::Act)
            return true;

        Quest const* quest = sObjectMgr.GetQuestTemplate(state.questId);
        if (quest && state.withLeader)
        {
            if (me->CanTakeQuest(quest, false) && me->CanAddQuest(quest, false))
                TakeWithLeader(me, quest, anchor);
        }
        else if (quest)
        {
            WorldPackets::Quest::QuestgiverAcceptQuest packet;
            packet.guid = giver->GetObjectGuid();
            packet.quest = state.questId;
            me->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);
        }

        if (!me->IsCurrentQuest(state.questId))
            GiveUp(me, state, giver->GetObjectGuid(), "accept refused");
        GiverVisit::Acted(state.visit, now);
        Clear(state);
        state.nextSearchMs = 0;
        return true;
    }
}
}
