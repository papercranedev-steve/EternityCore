#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "Log.h"
#include <algorithm>

namespace Life
{
namespace Requests
{
    namespace
    {
        std::string Clean(std::string text)
        {
            for (char& c : text)
                if (c == ';' || c == ',' || c == '=' || c == '\n' || c == '\r')
                    c = ' ';
            return text;
        }

        // race:gender:class, so the popup can draw the speaker when the client has no unit for them
        std::string Looks(Player const* bot)
        {
            return std::to_string(bot->GetRace()) + ":" + std::to_string(bot->GetGender()) + ":" +
                   std::to_string(bot->GetClass());
        }

        std::string OpenLine(Player const* bot, uint32 id, Ask const& ask, uint32 timeoutMs)
        {
            std::string options;
            for (Option const& o : ask.options)
            {
                if (!options.empty())
                    options += ",";
                options += Clean(o.key) + "=" + Clean(o.label);
            }
            std::string text = ask.text;
            std::replace(text.begin(), text.end(), '\n', ' ');
            return "PBREQ;" + std::string(bot->GetName()) + ";" + std::to_string(id) + ";open;" + DefOf(ask.intent).key + ";" +
                   std::to_string((timeoutMs + 999) / 1000) + ";" + Looks(bot) + ";" + options + ";" + text;
        }
    }

    Board& Board::Instance()
    {
        static Board instance;
        return instance;
    }

    bool Board::MayAsk(ObjectGuid bot, Intent intent) const
    {
        std::lock_guard<std::mutex> guard(m_lock);
        for (auto const& itr : m_open)
            if (itr.second.bot == bot)
                return false;
        auto quiet = m_quiet.find({ bot, intent });
        return quiet == m_quiet.end() || WorldTimer::getMSTime() >= quiet->second.untilMs;
    }

    uint32 Board::Open(Player* bot, Player* leader, Ask const& ask)
    {
        if (!bot || !leader || ask.options.empty())
            return 0;
        if (!MayAsk(bot->GetObjectGuid(), ask.intent))
            return 0;

        Entry e;
        e.bot = bot->GetObjectGuid();
        e.leader = leader->GetObjectGuid();
        e.botName = bot->GetName();
        e.ask = ask;
        e.openedMs = WorldTimer::getMSTime();
        e.timeoutMs = ask.timeoutMs ? ask.timeoutMs : DefOf(ask.intent).timeoutMs;
        if (e.ask.fallback.empty())
            e.ask.fallback = ask.options.back().key;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            DropStale(e.openedMs);
            e.id = m_nextId++;
            m_open[e.id] = e;
        }
        BotShared::BotChat::SystemTo(leader, OpenLine(bot, e.id, e.ask, e.timeoutMs));
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Request] %s asks %s (%s, #%u): %s",
            e.botName.c_str(), leader->GetName(), DefOf(ask.intent).key, e.id, ask.text.c_str());
        return e.id;
    }

    Status Board::Poll(uint32 id)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        Status status;
        auto itr = m_open.find(id);
        if (itr == m_open.end())
            return status;
        Entry& e = itr->second;
        if (e.state == State::Open && WorldTimer::getMSTimeDiff(e.openedMs, WorldTimer::getMSTime()) >= e.timeoutMs)
        {
            e.state = State::Expired;
            e.answer = e.ask.fallback;
        }
        status.state = e.state;
        status.answer = e.answer;
        return status;
    }

    void Board::Close(uint32 id, Outcome outcome)
    {
        Entry closed;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            auto itr = m_open.find(id);
            if (itr == m_open.end())
                return;
            closed = itr->second;
            m_open.erase(itr);

            IntentDef const& def = DefOf(closed.ask.intent);
            Quiet& quiet = m_quiet[{ closed.bot, closed.ask.intent }];
            uint32 const now = WorldTimer::getMSTime();
            switch (outcome)
            {
                case Outcome::Accepted:
                    quiet.declines = 0;
                    break;
                case Outcome::Declined:
                    quiet.declines = std::min(quiet.declines + 1, MAX_DECLINE_STEPS);
                    quiet.untilMs = now + def.declineBackoffMs * quiet.declines;
                    break;
                case Outcome::Expired:
                    quiet.untilMs = now + def.expireBackoffMs;
                    break;
                case Outcome::Cancelled:
                    break;
            }
        }
        if (closed.answer.empty())
            closed.answer = outcome == Outcome::Cancelled ? "cancel" : closed.ask.fallback;
        SendClose(closed);
    }

    bool Board::Reply(Player const* from, uint32 id, std::string const& key, std::string& error)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_open.find(id);
        if (itr == m_open.end() || itr->second.state != State::Open)
        {
            error = "notfound";
            return false;
        }
        Entry& e = itr->second;
        if (!from || from->GetObjectGuid() != e.leader)
        {
            error = "notyours";
            return false;
        }
        bool const known = std::any_of(e.ask.options.begin(), e.ask.options.end(),
                                       [&key](Option const& o) { return o.key == key; });
        if (!known)
        {
            error = "badoption";
            return false;
        }
        e.state = State::Answered;
        e.answer = key;
        return true;
    }

    void Board::Forget(ObjectGuid bot)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        for (auto itr = m_open.begin(); itr != m_open.end();)
        {
            if (itr->second.bot == bot)
                itr = m_open.erase(itr);
            else
                ++itr;
        }
        for (auto itr = m_quiet.begin(); itr != m_quiet.end();)
        {
            if (itr->first.first == bot)
                itr = m_quiet.erase(itr);
            else
                ++itr;
        }
    }

    void Board::DropStale(uint32 now)
    {
        for (auto itr = m_open.begin(); itr != m_open.end();)
        {
            if (WorldTimer::getMSTimeDiff(itr->second.openedMs, now) > itr->second.timeoutMs + STALE_MS)
                itr = m_open.erase(itr);
            else
                ++itr;
        }
    }

    void Board::SendClose(Entry const& e)
    {
        if (Player* leader = sObjectAccessor.FindPlayer(e.leader))
            BotShared::BotChat::SystemTo(leader, "PBREQ;" + e.botName + ";" + std::to_string(e.id) + ";close;" + Clean(e.answer));
    }
}
}
