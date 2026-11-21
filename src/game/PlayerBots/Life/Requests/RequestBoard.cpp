#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "Log.h"
#include <algorithm>
#include <set>

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

        uint32 Seconds(uint32 ms)
        {
            return (ms + 999) / 1000;
        }

        // timeoutSec 0: it waits behind the leader's earlier questions (a "turn" line starts it)
        std::string OpenLine(Player const* bot, uint32 id, Ask const& ask, uint32 timeoutSec)
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
                   std::to_string(timeoutSec) + ";" + Looks(bot) + ";" + options + ";" + text;
        }

        void Send(std::vector<std::pair<ObjectGuid, std::string>> const& lines)
        {
            for (auto const& line : lines)
                if (Player* leader = sObjectAccessor.FindPlayer(line.first))
                    BotShared::BotChat::SystemTo(leader, line.second);
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
        e.timeoutMs = ask.timeoutMs ? ask.timeoutMs : DefOf(ask.intent).timeoutMs;
        if (e.ask.fallback.empty())
            e.ask.fallback = ask.options.back().key;

        Lines lines;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            uint32 const now = WorldTimer::getMSTime();
            DropStale(now);
            Advance(now, lines);
            e.id = m_nextId++;
            e.started = !LeaderBusy(e.leader);
            e.startedMs = now;
            m_open[e.id] = e;
        }
        Send(lines);
        BotShared::BotChat::SystemTo(leader, OpenLine(bot, e.id, e.ask, e.started ? Seconds(e.timeoutMs) : 0));
        for (Ask::Note const& note : e.ask.notes)
            BotShared::BotChat::SystemTo(leader, "PBREQ;" + e.botName + ";" + std::to_string(e.id) + ";note;" +
                                                 std::to_string(note.tier) + ";" + Clean(note.text));
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Request] %s asks %s (%s, #%u%s): %s",
            e.botName.c_str(), leader->GetName(), DefOf(ask.intent).key, e.id, e.started ? "" : ", waits its turn",
            ask.text.c_str());
        return e.id;
    }

    Status Board::Poll(uint32 id)
    {
        Status status;
        Lines lines;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            Advance(WorldTimer::getMSTime(), lines);
            auto itr = m_open.find(id);
            if (itr != m_open.end())
            {
                status.state = itr->second.state;
                status.answer = itr->second.answer;
            }
        }
        Send(lines);
        return status;
    }

    void Board::Close(uint32 id, Outcome outcome)
    {
        Entry closed;
        Lines lines;
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
            Advance(now, lines);
        }
        if (closed.answer.empty())
            closed.answer = outcome == Outcome::Cancelled ? "cancel" : closed.ask.fallback;
        SendClose(closed);
        Send(lines);
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
        // (the next question starts on the next Advance: the asking bots poll every tick)
        return true;
    }

    void Board::Forget(ObjectGuid bot)
    {
        Lines lines;
        std::vector<Entry> dropped;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            for (auto itr = m_open.begin(); itr != m_open.end();)
            {
                if (itr->second.bot == bot)
                {
                    dropped.push_back(itr->second);
                    itr = m_open.erase(itr);
                }
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
            Advance(WorldTimer::getMSTime(), lines);
        }
        // The leader's popup goes too: a bot that left (a test scenario's reset) left its question
        // on his screen, in front of the next bots' questions.
        for (Entry& e : dropped)
        {
            e.answer = "cancel";
            SendClose(e);
        }
        Send(lines);
    }

    bool Board::LeaderBusy(ObjectGuid leader) const
    {
        return std::any_of(m_open.begin(), m_open.end(), [&leader](auto const& itr)
        {
            return itr.second.leader == leader && itr.second.started && itr.second.state == State::Open;
        });
    }

    void Board::Advance(uint32 now, Lines& out)
    {
        for (auto& itr : m_open)
        {
            Entry& e = itr.second;
            if (e.started && e.state == State::Open && WorldTimer::getMSTimeDiff(e.startedMs, now) >= e.timeoutMs)
            {
                e.state = State::Expired;
                e.answer = e.ask.fallback;
            }
        }

        // the oldest waiting question of each leader whose earlier one is done (ids grow: map order)
        std::set<ObjectGuid> busy;
        for (auto const& itr : m_open)
            if (itr.second.started && itr.second.state == State::Open)
                busy.insert(itr.second.leader);
        for (auto& itr : m_open)
        {
            Entry& e = itr.second;
            if (e.started || e.state != State::Open || busy.count(e.leader))
                continue;
            e.started = true;
            e.startedMs = now;
            busy.insert(e.leader);
            out.emplace_back(e.leader, "PBREQ;" + e.botName + ";" + std::to_string(e.id) + ";turn;" +
                                       std::to_string(Seconds(e.timeoutMs)));
        }
    }

    void Board::DropStale(uint32 now)
    {
        for (auto itr = m_open.begin(); itr != m_open.end();)
        {
            Entry const& e = itr->second;
            if (e.started && WorldTimer::getMSTimeDiff(e.startedMs, now) > e.timeoutMs + STALE_MS)
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
