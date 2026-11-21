/*
 * Bot daily life (PlayerBots/Life), requests: a bot asks its leader something and the player
 * answers in a popup of the PartyBot addon. Soul companions doc §5 (stage S5).
 *
 * Lifecycle (the asking bot drives it from its map thread):
 *   Open    the bot asks (one open request per bot; not while its intent is backing off).
 *           The leader gets "PBREQ;<bot>;<id>;open;<intent>;<timeoutSec>;<race:gender:class>;<options>;<text>",
 *           options "key=label,key=label" (labels without ; , =), the text last. Race, gender
 *           and class let the popup draw the speaker when the client has no unit for the bot.
 *   Turn    a leader answers one question at a time: the timeout runs only for the oldest open
 *           question of each leader. Others wait (timeoutSec 0 in their open line) and get
 *           "PBREQ;<bot>;<id>;turn;<timeoutSec>" when the earlier one is answered or expired.
 *   Poll    Open until the leader answers (".partybot reply <id> <option>": Reply, any thread)
 *           or the timeout passes once its turn came: Expired with the request's fallback.
 *           Players often ignore popups: every request has a fallback.
 *   Notes   lines under the question in the popup (Ask::notes), each its own system line.
 *   Close   the bot is done with it, with how it went (Accepted, Declined, Expired, Cancelled):
 *           the leader gets "PBREQ;<bot>;<id>;close;<answer>", and Declined / Expired start the
 *           intent's quiet time for this bot (Intents.h). Accepted resets the declines.
 * Requests nobody closes (the bot logged out) are dropped STALE_MS after their timeout.
 *
 * What the bot says in party chat is the caller's (Phrases.h): the popup is for the answer,
 * the chat is the character talking.
 */

#ifndef MANGOS_LIFE_REQUEST_BOARD_H
#define MANGOS_LIFE_REQUEST_BOARD_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Requests/Intents.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>

class Player;

namespace Life
{
namespace Requests
{
    struct Option
    {
        std::string key;        // what ".partybot reply" sends back ("yes", "2"); the addon draws
                                // "yes" / "no" as agree / decline (green tick, red cross)
        std::string label;      // the button
    };

    struct Ask
    {
        Intent intent = Intent::Errand;
        std::string text;               // the popup's question
        std::vector<Option> options;
        std::string fallback;           // the answer when nobody answers (one of the keys)
        uint32 timeoutMs = 0;           // 0: the intent's
        // Lines under the question, in the popup only (the party chat gets the text alone):
        // "PBREQ;<bot>;<id>;note;<tier>;<text>" after the open line, in order. `tier` picks
        // the colour (Life/Errands/Trip/LessonPriority tiers 1-6; 0 = plain ink).
        struct Note
        {
            uint8 tier = 0;
            std::string text;
        };
        std::vector<Note> notes;
    };

    enum class State : uint8
    {
        None,       // no such request (closed, dropped)
        Open,
        Answered,
        Expired,    // answer = the fallback
    };

    enum class Outcome : uint8
    {
        Accepted,
        Declined,
        Expired,
        Cancelled,  // the bot no longer needs it (combat, the party moved on)
    };

    struct Status
    {
        State state = State::None;
        std::string answer;
    };

    class Board
    {
    public:
        static Board& Instance();

        static constexpr uint32 STALE_MS = 5 * MINUTE * IN_MILLISECONDS;

        // False while the bot has an open request or the intent backs off for it.
        bool MayAsk(ObjectGuid bot, Intent intent) const;
        // The request id, 0 when it may not ask now.
        uint32 Open(Player* bot, Player* leader, Ask const& ask);
        Status Poll(uint32 id);
        void Close(uint32 id, Outcome outcome);

        // `from` answers request `id` with option `key`. False: no open request of a bot of
        // `from`'s (`error`: "notfound", "notyours", "badoption").
        bool Reply(Player const* from, uint32 id, std::string const& key, std::string& error);

        // The bot is gone for good (a temporary bot logged out; its guid is given out again):
        // drops its open requests (a close line "cancel" to the leader, so his popup goes too) and
        // its quiet times.
        void Forget(ObjectGuid bot);

    private:
        Board() = default;

        struct Entry
        {
            uint32 id = 0;
            ObjectGuid bot;
            ObjectGuid leader;
            std::string botName;
            Ask ask;
            bool started = false;       // its clock runs (false: waits behind the leader's earlier question)
            uint32 startedMs = 0;
            uint32 timeoutMs = 0;
            State state = State::Open;
            std::string answer;
        };

        struct Quiet
        {
            uint32 untilMs = 0;
            uint32 declines = 0;
        };

        using Lines = std::vector<std::pair<ObjectGuid, std::string>>;  // leader, system message

        // With m_lock held. A started question of this leader is still open.
        bool LeaderBusy(ObjectGuid leader) const;
        // With m_lock held: expires started questions past their timeout, starts each leader's
        // next waiting one (its "turn" line goes to `out`, sent after the lock is released).
        void Advance(uint32 now, Lines& out);
        void DropStale(uint32 now);
        static void SendClose(Entry const& e);

        mutable std::mutex m_lock;
        uint32 m_nextId = 1;
        std::map<uint32, Entry> m_open;
        std::map<std::pair<ObjectGuid, Intent>, Quiet> m_quiet;
    };
}
}

#define sRequestBoard Life::Requests::Board::Instance()

#endif
