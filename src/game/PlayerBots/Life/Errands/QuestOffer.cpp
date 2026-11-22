#include "Life/Errands/QuestOffer.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/GiverVisit.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/GameNames.h"
#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "Shared/QuestRules.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Timer.h"
#include "Log.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cstring>
#include <list>

namespace Life
{
namespace QuestOffer
{
    namespace
    {
        namespace Phrases = BotShared::Phrases;
        namespace I18n = BotShared::I18n;

        // The quests a giver gives (its quest relations).
        std::vector<uint32> QuestsOf(WorldObject const* giver)
        {
            std::vector<uint32> quests;
            QuestRelationsMapBounds const bounds = giver->IsGameObject() ?
                sObjectMgr.GetGOQuestRelationsMapBounds(giver->GetEntry()) :
                sObjectMgr.GetCreatureQuestRelationsMapBounds(giver->GetEntry());
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
                quests.push_back(itr->second);
            return quests;
        }

        // The quests of `giver` to ask the leader about.
        std::vector<uint32> Candidates(Player* me, Player const* leader, QuestOfferState const& state,
                                       WorldObject const* giver, uint32 now)
        {
            std::vector<uint32> out;
            for (uint32 questId : QuestsOf(giver))
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (!quest || !Offerable(quest) || state.approved.count(questId) || state.declined.count(questId))
                    continue;
                auto const quiet = state.quietUntilMs.find(questId);
                if (quiet != state.quietUntilMs.end() && now < quiet->second)
                    continue;
                if (me->IsCurrentQuest(questId) || !me->CanTakeQuest(quest, false) || !me->CanAddQuest(quest, false))
                    continue;
                if (leader->IsCurrentQuest(questId) || !BotShared::QuestRules::Barred(BotShared::QuestRules::WhyNot(leader, quest)))
                    continue;
                if (std::find(out.begin(), out.end(), questId) == out.end())
                    out.push_back(questId);
            }
            return out;
        }

        class GiverCheck
        {
        public:
            GiverCheck(Player* me, Player const* leader) : m_me(me), m_leader(leader) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && c->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) && !c->IsHostileTo(m_me) &&
                       m_me->IsWithinDist(c, SEARCH_YD, false) && GiverVisit::LeaderBy(m_leader, c);
            }
            bool operator()(GameObject* go)
            {
                return go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER &&
                       m_me->IsWithinDist(go, SEARCH_YD, false) && GiverVisit::LeaderBy(m_leader, go);
            }

        private:
            Player* m_me;
            Player const* m_leader;
        };

        // The giver the leader stands by that has quests to ask about, the nearest to him (every
        // bot of the party finds the same one). `quests`: those quests.
        WorldObject* FindGiver(Player* me, Player const* leader, QuestOfferState const& state, uint32 now,
                               std::vector<uint32>& quests)
        {
            GiverCheck check(me, leader);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<GiverCheck> creatureSearcher(creatures, check);
            Cell::VisitGridObjects(me, creatureSearcher, SEARCH_YD);
            std::list<GameObject*> objects;
            MaNGOS::GameObjectListSearcher<GiverCheck> objectSearcher(objects, check);
            Cell::VisitGridObjects(me, objectSearcher, SEARCH_YD);

            std::vector<WorldObject*> givers(creatures.begin(), creatures.end());
            givers.insert(givers.end(), objects.begin(), objects.end());
            std::sort(givers.begin(), givers.end(), [leader](WorldObject* a, WorldObject* b)
            {
                return leader->GetDistance(a) < leader->GetDistance(b);
            });
            for (WorldObject* g : givers)
            {
                quests = Candidates(me, leader, state, g, now);
                if (!quests.empty())
                    return g;
            }
            quests.clear();
            return nullptr;
        }

        std::string Joined(I18n::Lang lang, std::vector<std::string> const& words)
        {
            std::string const sep = Phrases::Word(lang, "place.comma");
            std::string out;
            for (std::string const& w : words)
                out += (out.empty() ? "" : sep) + w;
            return out;
        }
    }

    bool Offerable(Quest const* quest)
    {
        if (quest->HasSpecialFlag(QuestSpecialFlags(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT | QUEST_SPECIAL_FLAG_TIMED |
                                                    QUEST_SPECIAL_FLAG_MIRROR)) || quest->GetLimitTime())
            return false;
        for (uint32 spell : quest->ReqSpell)
            if (spell)
                return false;
        return true;
    }

    // ---- the desk ----

    Desk& Desk::Instance()
    {
        static Desk instance;
        return instance;
    }

    void Desk::Settle(LeaderQueue& q, uint32 now)
    {
        if (!q.requestId)
            return;
        Requests::Status const status = sRequestBoard.Poll(q.requestId);
        if (status.state == Requests::State::Open)
            return;
        Answer answer = Answer::Silent;
        Requests::Outcome outcome = Requests::Outcome::Expired;
        if (status.state == Requests::State::Answered)
        {
            bool const yes = status.answer == "yes";
            answer = yes ? Answer::Yes : Answer::No;
            outcome = yes ? Requests::Outcome::Accepted : Requests::Outcome::Declined;
        }
        sRequestBoard.Close(q.requestId, outcome);
        q.requestId = 0;
        for (Asker const& a : q.bots)
        {
            Reply r;
            r.answer = answer;
            r.quests = a.quests;
            q.results[a.bot] = { r, now };
        }
        q.bots.clear();
    }

    void Desk::Tidy(uint32 now)
    {
        for (auto itr = m_leaders.begin(); itr != m_leaders.end();)
        {
            LeaderQueue& q = itr->second;
            for (auto r = q.results.begin(); r != q.results.end();)
                r = WorldTimer::getMSTimeDiff(r->second.second, now) > RESULT_KEEP_MS ? q.results.erase(r) : std::next(r);
            q.bots.erase(std::remove_if(q.bots.begin(), q.bots.end(), [now](Asker const& a)
            {
                return WorldTimer::getMSTimeDiff(a.seenMs, now) > SEEN_MS;
            }), q.bots.end());
            if (q.bots.empty() && q.requestId)
            {
                // Nobody waits for the answer any more: the popup goes.
                sRequestBoard.Close(q.requestId, Requests::Outcome::Cancelled);
                q.requestId = 0;
            }
            if (q.bots.empty() && q.results.empty())
                itr = m_leaders.erase(itr);
            else
                ++itr;
        }
    }

    Reply Desk::Want(Player* bot, Player* leader, ObjectGuid giver, std::vector<uint32> const& quests,
                     std::vector<std::string> const& tags)
    {
        ObjectGuid const botGuid = bot->GetObjectGuid();
        uint32 const now = WorldTimer::getMSTime();
        std::string said;
        Reply reply;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            LeaderQueue& q = m_leaders[leader->GetObjectGuid()];
            Settle(q, now);
            Tidy(now);
            LeaderQueue& queue = m_leaders[leader->GetObjectGuid()];    // (Tidy may have dropped it)

            auto const result = queue.results.find(botGuid);
            if (result != queue.results.end())
            {
                reply = result->second.first;
                queue.results.erase(result);
                return reply;
            }

            auto const mine = std::find_if(queue.bots.begin(), queue.bots.end(), [botGuid](Asker const& a) { return a.bot == botGuid; });
            if (queue.requestId)
            {
                // Asked already: the named ones wait for the answer, the others for the next question.
                if (mine != queue.bots.end())
                    mine->seenMs = now;
                return reply;
            }
            if (queue.bots.empty())
            {
                queue.giver = giver;
                queue.firstMs = now;
            }
            else if (queue.giver != giver)
                return reply;   // the others' giver first
            if (mine != queue.bots.end())
            {
                mine->quests = quests;
                mine->seenMs = now;
            }
            else
            {
                Asker a;
                a.bot = botGuid;
                a.quests = quests;
                a.seenMs = now;
                queue.bots.push_back(a);
            }

            // The first in the queue asks for everyone, once the others had a moment to join.
            if (queue.bots.front().bot != botGuid || WorldTimer::getMSTimeDiff(queue.firstMs, now) < GATHER_MS)
                return reply;

            I18n::Lang const lang = I18n::OfParty(bot);
            std::vector<uint32> all;
            std::vector<std::string> names;
            for (Asker const& a : queue.bots)
            {
                if (Player* member = sObjectAccessor.FindPlayer(a.bot))
                    names.push_back(member->GetName());
                for (uint32 questId : a.quests)
                    if (std::find(all.begin(), all.end(), questId) == all.end())
                        all.push_back(questId);
            }
            bool allDone = true;
            std::vector<std::string> titles;
            for (uint32 questId : all)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                char const* why = quest ? BotShared::QuestRules::WhyNot(leader, quest) : nullptr;
                allDone = allDone && why && std::strcmp(why, "done") == 0;
                titles.push_back(Phrases::Text(lang, "questoffer.title",
                    { { "title", BotShared::GameNames::QuestTitle(questId, leader) } }));
            }

            Phrases::Vars vars;
            vars["leader"] = leader->GetName();
            vars["quests"] = Joined(lang, titles);
            vars["names"] = Joined(lang, names);
            std::string key = allDone ? "questoffer.ask.done" : "questoffer.ask";
            if (queue.bots.size() > 1)
                key += ".many";

            Requests::Ask ask;
            ask.intent = Requests::Intent::QuestOffer;
            ask.text = Phrases::Say(lang, key, tags, vars);
            ask.options = {
                { "yes", Phrases::Word(lang, "questoffer.opt.yes") },
                { "no", Phrases::Word(lang, "questoffer.opt.no") },
            };
            ask.fallback = "no";
            queue.requestId = sRequestBoard.Open(bot, leader, ask);
            if (queue.requestId)
                said = ask.text;
        }
        if (!said.empty())
            BotShared::BotChat::SayToParty(bot, said);
        return reply;
    }

    void Desk::Leave(ObjectGuid leader, ObjectGuid bot)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_leaders.find(leader);
        if (itr == m_leaders.end())
            return;
        LeaderQueue& q = itr->second;
        Settle(q, WorldTimer::getMSTime());
        q.bots.erase(std::remove_if(q.bots.begin(), q.bots.end(), [bot](Asker const& a) { return a.bot == bot; }), q.bots.end());
        Tidy(WorldTimer::getMSTime());
    }

    // ---- the errand ----

    void Stop(Player* me, QuestOfferState& state)
    {
        if (state.queuedAt.IsEmpty())
            return;
        sQuestOfferDesk.Leave(state.queuedAt, me->GetObjectGuid());
        state.queuedAt.Clear();
    }

    void Reset(QuestOfferState& state)
    {
        state = QuestOfferState();
    }

    std::vector<uint32> Approved(Player const* me, QuestOfferState const& state)
    {
        std::vector<uint32> quests;
        for (uint32 questId : state.approved)
            if (me->GetQuestStatus(questId) == QUEST_STATUS_NONE)
                quests.push_back(questId);
        return quests;
    }

    void Update(Player* me, QuestOfferState& state, Player* leader, std::vector<std::string> const& tags)
    {
        if (!leader || leader == me || leader->IsBot())
        {
            Stop(me, state);
            return;
        }
        if (state.leader != leader->GetObjectGuid())
        {
            Stop(me, state);
            Reset(state);
            state.leader = leader->GetObjectGuid();
        }
        if (!Errands::MayRun(me, leader, true) || leader->IsInCombat())
        {
            Stop(me, state);
            return;
        }

        uint32 const now = WorldTimer::getMSTime();
        if (now < state.nextLookMs)
            return;
        state.nextLookMs = now + LOOK_MS;

        std::vector<uint32> quests;
        WorldObject* giver = me->SatisfyQuestLog(false) ? FindGiver(me, leader, state, now, quests) : nullptr;
        if (!giver)
        {
            Stop(me, state);
            return;
        }

        Reply const reply = sQuestOfferDesk.Want(me, leader, giver->GetObjectGuid(), quests, tags);
        state.queuedAt = leader->GetObjectGuid();
        if (reply.answer == Answer::Wait)
            return;
        state.queuedAt.Clear();
        for (uint32 questId : reply.quests)
        {
            if (reply.answer == Answer::Yes)
                state.approved.insert(questId);
            else if (reply.answer == Answer::No)
                state.declined.insert(questId);
            else
                state.quietUntilMs[questId] = now + QUIET_MS;
        }
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestOffer] %s: %u quest(s) at %s: %s.", me->GetName(),
                 uint32(reply.quests.size()), giver->GetObjectGuid().GetString().c_str(),
                 reply.answer == Answer::Yes ? "yes" : reply.answer == Answer::No ? "no" : "no answer");
    }
}
}
