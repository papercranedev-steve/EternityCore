#include "Life/Errands/ProvisionDesk.h"
#include "Shared/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>

namespace Life
{
namespace Provision
{
    namespace
    {
        using BotShared::Provisions::Kind;

        bool Has(Kinds const& kinds, Kind kind)
        {
            return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
        }
    }

    // "水和面包", "面包", "水": what the bots ask for.
    std::string WhatOf(Kinds const& kinds, BotShared::I18n::Lang lang)
    {
        bool const drink = Has(kinds, Kind::Drink);
        bool const food = Has(kinds, Kind::Food);
        if (drink && food)
            return BotShared::Phrases::Word(lang, "supply.what.both");
        return drink ? BotShared::Phrases::Word(lang, "supply.what.drink") : BotShared::Phrases::Word(lang, "supply.what.food");
    }

    Desk& Desk::Instance()
    {
        static Desk instance;
        return instance;
    }

    uint32 Desk::Post(ObjectGuid giver, ObjectGuid taker, Kinds const& kinds)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        uint32 const now = WorldTimer::getMSTime();
        DropStale(now);
        for (auto const& itr : m_orders)
            if (itr.second.taker == taker)
                return 0;
        Order order;
        order.id = m_nextId++;
        order.giver = giver;
        order.taker = taker;
        order.kinds = kinds;
        order.postedMs = now;
        m_orders[order.id] = order;
        return order.id;
    }

    OrderState Desk::StateOf(uint32 id) const
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_orders.find(id);
        return itr != m_orders.end() ? itr->second.state : OrderState::None;
    }

    bool Desk::TakeNext(ObjectGuid giver, Order& out)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        DropStale(WorldTimer::getMSTime());
        for (auto& itr : m_orders)
        {
            Order& order = itr.second;
            if (order.giver != giver || order.state != OrderState::Open || !order.asked)
                continue;
            order.state = OrderState::Taken;
            out = order;
            return true;
        }
        return false;
    }

    void Desk::MarkAsked(uint32 id)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_orders.find(id);
        if (itr != m_orders.end())
            itr->second.asked = true;
    }

    void Desk::Finish(uint32 id, bool done)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_orders.find(id);
        if (itr != m_orders.end())
            itr->second.state = done ? OrderState::Done : OrderState::Failed;
    }

    void Desk::Drop(uint32 id)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_orders.erase(id);
    }

    LeaderStep Desk::Want(Player* bot, Player* leader, Kinds const& kinds, std::vector<std::string> const& tags)
    {
        ObjectGuid const botGuid = bot->GetObjectGuid();
        uint32 const now = WorldTimer::getMSTime();
        std::string said;
        LeaderStep step = LeaderStep::Wait;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            DropStale(now);
            LeaderQueue& q = m_leaders[leader->GetObjectGuid()];
            if (q.refused)
                return LeaderStep::Refused;
            if (q.quietUntilMs && now < q.quietUntilMs)
                return LeaderStep::Quiet;
            q.quietUntilMs = 0;
            q.seenMs[botGuid] = now;
            q.bots.erase(std::remove_if(q.bots.begin(), q.bots.end(), [&](ObjectGuid const& guid)
            {
                auto seen = q.seenMs.find(guid);
                bool const gone = seen == q.seenMs.end() || WorldTimer::getMSTimeDiff(seen->second, now) > SEEN_MS;
                if (gone)
                    q.seenMs.erase(guid);
                return gone;
            }), q.bots.end());
            if (std::find(q.bots.begin(), q.bots.end(), botGuid) == q.bots.end())
            {
                if (q.bots.empty())
                    q.firstMs = now;
                q.bots.push_back(botGuid);
            }

            if (q.requestId)
            {
                Requests::Status const status = sRequestBoard.Poll(q.requestId);
                if (status.state == Requests::State::Open)
                    return LeaderStep::Wait;
                uint32 const id = q.requestId;
                q.requestId = 0;
                if (status.state == Requests::State::Answered && status.answer == "yes")
                {
                    sRequestBoard.Close(id, Requests::Outcome::Accepted);
                    q.accepted = true;
                }
                else if (status.state == Requests::State::Answered)
                {
                    sRequestBoard.Close(id, Requests::Outcome::Declined);
                    q.refused = true;
                    q.refusedMs = now;
                    q.bots.clear();
                    q.seenMs.clear();
                    return LeaderStep::Refused;
                }
                else
                {
                    sRequestBoard.Close(id, Requests::Outcome::Expired);
                    q.quietUntilMs = now + QUIET_MS;
                    q.bots.clear();
                    q.seenMs.clear();
                    return LeaderStep::Quiet;
                }
            }

            if (q.accepted)
                return q.bots.front() == botGuid ? LeaderStep::MyTurn : LeaderStep::Wait;

            // The first in the queue asks for everyone short now, once the others had a
            // moment to join.
            if (q.bots.front() != botGuid || WorldTimer::getMSTimeDiff(q.firstMs, now) < GATHER_MS)
                return LeaderStep::Wait;

            BotShared::Phrases::Vars vars;
            vars["leader"] = leader->GetName();
            vars["what"] = WhatOf(kinds, BotShared::I18n::OfParty(bot));
            std::string names;
            for (ObjectGuid const& guid : q.bots)
                if (Player* member = sObjectAccessor.FindPlayer(guid))
                    names += (names.empty() ? "" : BotShared::Phrases::Word(BotShared::I18n::OfParty(bot), "place.comma")) + member->GetName();
            vars["names"] = names;

            Requests::Ask ask;
            ask.intent = Requests::Intent::Supply;
            ask.text = BotShared::Phrases::Say(BotShared::I18n::OfParty(bot), q.bots.size() > 1 ? "supply.askmage.many" : "supply.askmage", tags, vars);
            ask.options = {
                { "yes", BotShared::Phrases::Word(BotShared::I18n::OfParty(bot), "supply.opt.make") },
                { "no", BotShared::Phrases::Word(BotShared::I18n::OfParty(bot), "supply.opt.later") },
            };
            ask.fallback = "no";
            q.requestId = sRequestBoard.Open(bot, leader, ask);
            if (q.requestId)
                said = ask.text;
            step = LeaderStep::Wait;
        }
        if (!said.empty())
            BotShared::BotChat::SayToParty(bot, said);
        return step;
    }

    void Desk::Served(ObjectGuid leader, ObjectGuid bot, bool given, bool failed)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_leaders.find(leader);
        if (itr == m_leaders.end())
            return;
        LeaderQueue& q = itr->second;
        q.bots.erase(std::remove(q.bots.begin(), q.bots.end(), bot), q.bots.end());
        q.seenMs.erase(bot);
        uint32 const now = WorldTimer::getMSTime();
        if (!given && !failed)
        {
            q.refused = true;
            q.refusedMs = now;
            q.bots.clear();
            q.seenMs.clear();
        }
        if (q.bots.empty())
            q.accepted = false;
        Tidy(leader, now);
    }

    void Desk::Leave(ObjectGuid leader, ObjectGuid bot)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_leaders.find(leader);
        if (itr == m_leaders.end())
            return;
        LeaderQueue& q = itr->second;
        q.bots.erase(std::remove(q.bots.begin(), q.bots.end(), bot), q.bots.end());
        q.seenMs.erase(bot);
        if (q.bots.empty())
            q.accepted = false;
        Tidy(leader, WorldTimer::getMSTime());
    }

    void Desk::Tidy(ObjectGuid leader, uint32 now)
    {
        auto itr = m_leaders.find(leader);
        if (itr == m_leaders.end())
            return;
        LeaderQueue const& q = itr->second;
        bool const quiet = q.quietUntilMs && now < q.quietUntilMs;
        if (q.bots.empty() && !q.requestId && !q.refused && !quiet)
            m_leaders.erase(itr);
    }

    void Desk::DropStale(uint32 now)
    {
        for (auto itr = m_orders.begin(); itr != m_orders.end();)
        {
            if (WorldTimer::getMSTimeDiff(itr->second.postedMs, now) > STALE_MS)
                itr = m_orders.erase(itr);
            else
                ++itr;
        }
        for (auto itr = m_leaders.begin(); itr != m_leaders.end();)
        {
            LeaderQueue const& q = itr->second;
            bool const refusedOld = q.refused && WorldTimer::getMSTimeDiff(q.refusedMs, now) > REFUSED_KEEP_MS;
            bool const quietOver = q.quietUntilMs && now >= q.quietUntilMs && q.bots.empty() && !q.requestId;
            if (refusedOld || quietOver)
                itr = m_leaders.erase(itr);
            else
                ++itr;
        }
    }
}
}
