/*
 * Bot daily life (PlayerBots/Life): where bots of one party meet about conjured supplies (user
 * 2026-11-20). Two boards:
 *
 * Orders (bot to bot): a bot short of food or drink asks a mage bot of its party instead of
 * buying (Life/Errands/AskProvisions). The taker posts an order for the giver; the giver takes
 * its orders oldest first (Life/Errands/GiveProvisions) and finishes each, done or failed; the
 * taker reads how it went and drops it. Orders nobody drops are dropped after STALE_MS.
 *
 * The human mage (bots to their leader): the bots short of food or drink queue up for the
 * leader. One question for all of them, asked by the first in the queue after GATHER_MS (the
 * others short at that moment are named in it). Yes: one bot after the other trades with the
 * leader (Turn). No, or the leader gives nothing in a trade: refused; the bots keep that for
 * their session (they read it within REFUSED_KEEP_MS). No answer: quiet for QUIET_MS.
 *
 * Any thread (one mutex): a party's bots may run on different map threads.
 */

#ifndef MANGOS_LIFE_PROVISION_DESK_H
#define MANGOS_LIFE_PROVISION_DESK_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Shared/Provisions.h"
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class Player;

namespace Life
{
namespace Provision
{
    using Kinds = std::vector<BotShared::Provisions::Kind>;

    // What is asked for, in words (Phrases supply.what.*: "水和面包", "面包", "水").
    std::string WhatOf(Kinds const& kinds);

    enum class OrderState : uint8
    {
        None,       // no such order (dropped)
        Open,       // the giver has not taken it yet
        Taken,
        Done,
        Failed,
    };

    struct Order
    {
        uint32 id = 0;
        ObjectGuid giver;
        ObjectGuid taker;
        Kinds kinds;
        OrderState state = OrderState::Open;
        uint32 postedMs = 0;
        bool asked = false;     // the taker stands by the giver and has said it (white /say)
    };

    // What a bot in the human mage's queue does now.
    enum class LeaderStep : uint8
    {
        Wait,       // the question is open, or another bot trades first
        MyTurn,     // trade with the leader now
        Refused,    // he said no / gave nothing: do not ask him again this session
        Quiet,      // he did not answer: not now
    };

    class Desk
    {
    public:
        static Desk& Instance();

        static constexpr uint32 STALE_MS = 5 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 GATHER_MS = 2 * IN_MILLISECONDS;
        static constexpr uint32 QUIET_MS = 30 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 REFUSED_KEEP_MS = MINUTE * IN_MILLISECONDS;
        // A queued bot that has not asked for this long left (logged out, stuck in a fight).
        static constexpr uint32 SEEN_MS = 15 * IN_MILLISECONDS;

        // ---- orders ----
        // The order id; 0 when the taker has an order already.
        uint32 Post(ObjectGuid giver, ObjectGuid taker, Kinds const& kinds);
        OrderState StateOf(uint32 id) const;
        // The taker has come up to the giver and asked aloud: the giver may take it now.
        void MarkAsked(uint32 id);
        // The giver's oldest open order that was asked, now taken. False: none.
        bool TakeNext(ObjectGuid giver, Order& out);
        void Finish(uint32 id, bool done);
        void Drop(uint32 id);

        // ---- the human mage ----
        // `bot` is short of `kinds` and asks its leader, a mage. `tags`: the bot's personality.
        LeaderStep Want(Player* bot, Player* leader, Kinds const& kinds, std::vector<std::string> const& tags);
        // The bot's trade with the leader is over: `given` false (he gave nothing, closed the
        // window) refuses for everyone; `failed` (the trade itself did not work) only moves on.
        void Served(ObjectGuid leader, ObjectGuid bot, bool given, bool failed);
        // The bot no longer waits (no longer short, combat, logged out).
        void Leave(ObjectGuid leader, ObjectGuid bot);

    private:
        Desk() = default;

        struct LeaderQueue
        {
            std::deque<ObjectGuid> bots;    // the first one trades first
            std::map<ObjectGuid, uint32> seenMs;    // its last Want (gone quiet: out of the queue)
            uint32 firstMs = 0;             // since the first bot queued
            uint32 requestId = 0;
            bool accepted = false;
            bool refused = false;
            uint32 refusedMs = 0;
            uint32 quietUntilMs = 0;
        };

        // With m_lock held.
        void DropStale(uint32 now);
        void Tidy(ObjectGuid leader, uint32 now);

        mutable std::mutex m_lock;
        uint32 m_nextId = 1;
        std::map<uint32, Order> m_orders;
        std::map<ObjectGuid, LeaderQueue> m_leaders;
    };
}
}

#define sProvisionDesk Life::Provision::Desk::Instance()

#endif
