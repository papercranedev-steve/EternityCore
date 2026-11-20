#include "Life/Errands/AskProvisions.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "Shared/Provisions.h"
#include "Shared/Supplies.h"
#include "Shared/Trade.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>

namespace Life
{
namespace AskProvisions
{
    namespace
    {
        using BotShared::Provisions::Kind;
        namespace Provisions = BotShared::Provisions;
        namespace Trade = BotShared::Trade;

        Provision::Kinds const FOOD_KINDS = { Kind::Drink, Kind::Food };

        // What `giver` makes that the bot uses; from a bot giver only when close to the bot's
        // level (else buying is better).
        Provision::Kinds MadeFor(Player const* giver, Player const* me, bool checkLevel)
        {
            Provision::Kinds kinds;
            for (Kind kind : FOOD_KINDS)
            {
                // (a mage makes its own)
                if (!Provisions::Uses(me, kind) || Provisions::MakesOwn(me, kind))
                    continue;
                SpellEntry const* recipe = Provisions::RecipeFor(giver, me, kind);
                if (!recipe || (checkLevel && !Provisions::CloseToLevel(me, Provisions::ProductOf(recipe))))
                    continue;
                kinds.push_back(kind);
            }
            return kinds;
        }

        Provision::Kinds ShortOf(Player const* me, Provision::Kinds const& kinds)
        {
            Provision::Kinds result;
            for (Kind kind : kinds)
                if (Provisions::Short(me, kind))
                    result.push_back(kind);
            return result;
        }

        void SetProvided(Player const* me, Provision::Kinds const& kinds)
        {
            std::set<BotShared::Goods::Kind> goods;
            for (Kind kind : kinds)
                goods.insert(kind == Kind::Drink ? BotShared::Goods::Kind::Drink : BotShared::Goods::Kind::Food);
            BotShared::Supplies::SetProvided(me->GetObjectGuid(), goods);
        }

        bool Quiet(std::map<ObjectGuid, uint32> const& quiet, ObjectGuid guid, uint32 now)
        {
            auto itr = quiet.find(guid);
            return itr != quiet.end() && now < itr->second;
        }

        // True once within NEAR_YD and standing.
        bool WalkUpTo(Player* me, AskProvisionsState& state, Player* target, uint32 now)
        {
            if (me->IsWithinDistInMap(target, NEAR_YD))
            {
                if (me->IsMoving())
                {
                    Errands::StopWalk(me);
                    return false;
                }
                return true;
            }
            if (state.approach.arrivedMs || !state.approach.sinceMs)
                Trip::Approach::Begin(state.approach, now);
            Trip::Destination dest;
            dest.x = target->GetPositionX();
            dest.y = target->GetPositionY();
            dest.z = target->GetPositionZ();
            dest.reachYd = 1.5f;
            Trip::Approach::Update(me, dest, state.approach, now);
            return false;
        }

        void DropOrder(AskProvisionsState& state, bool quietGiver, uint32 now)
        {
            sProvisionDesk.Drop(state.orderId);
            if (quietGiver)
                state.giverQuietUntilMs[state.giver] = now + GIVER_QUIET_MS;
            state.orderId = 0;
            state.orderKinds.clear();
            state.giver.Clear();
            state.asked = false;
        }

        bool FollowOrder(Player* me, AskProvisionsState& state, std::vector<std::string> const& tags, uint32 now)
        {
            Player* giver = me->GetMap()->GetPlayer(state.giver);
            Provision::OrderState const orderState = sProvisionDesk.StateOf(state.orderId);
            if (orderState == Provision::OrderState::Done)
            {
                DropOrder(state, false, now);
                state.nextAskMs = now + AFTER_GIFT_MS;
                BotShared::BotChat::Say(me, Phrases::Say("supply.thanks", tags, {}));
                return false;
            }
            if (orderState == Provision::OrderState::Failed || orderState == Provision::OrderState::None ||
                !giver || !giver->IsAlive() || WorldTimer::getMSTimeDiff(state.orderMs, now) > ORDER_MS)
            {
                DropOrder(state, true, now);
                return false;
            }
            if (!state.asked)
            {
                if (!WalkUpTo(me, state, giver, now))
                    return true;
                Phrases::Vars vars;
                vars["giver"] = giver->GetName();
                vars["what"] = Provision::WhatOf(state.orderKinds);
                BotShared::BotChat::Say(me, Phrases::Say("supply.botask", tags, vars));
                sProvisionDesk.MarkAsked(state.orderId);
                state.asked = true;
            }
            // (waits by the mage: it comes to trade)
            return true;
        }

        void EndLeaderTurn(Player* me, AskProvisionsState& state, Player* leader, bool given, bool failed)
        {
            sProvisionDesk.Served(leader->GetObjectGuid(), me->GetObjectGuid(), given, failed);
            state.queued = false;
            state.trading = false;
            state.failures = 0;
        }

        bool FollowLeader(Player* me, AskProvisionsState& state, Player* leader, Trade::Hands& hands,
                          std::vector<std::string> const& tags, uint32 now)
        {
            Provision::Kinds const shortKinds = ShortOf(me, MadeFor(leader, me, false));
            if (state.trading)
            {
                // (keeps its place in the queue)
                sProvisionDesk.Want(me, leader, shortKinds.empty() ? FOOD_KINDS : shortKinds, tags);
                switch (Trade::Update(me, hands))
                {
                    case Trade::Result::Busy:
                        return true;
                    case Trade::Result::Completed:
                    {
                        bool const given = Provisions::Held(me, Kind::Drink) > state.heldBefore[0] ||
                                           Provisions::Held(me, Kind::Food) > state.heldBefore[1];
                        EndLeaderTurn(me, state, leader, given, false);
                        if (given)
                        {
                            state.nextAskMs = now + AFTER_GIFT_MS;
                            BotShared::BotChat::SayToParty(me, Phrases::Say("supply.thanks", tags, {}));
                        }
                        else
                            state.leaderRefused = true;
                        return false;
                    }
                    case Trade::Result::Declined:
                        EndLeaderTurn(me, state, leader, false, false);
                        state.leaderRefused = true;
                        return false;
                    default:
                        state.trading = false;
                        if (++state.failures > MAX_RETRIES)
                        {
                            EndLeaderTurn(me, state, leader, true, true);
                            state.leaderQuietUntilMs = now + GIVER_QUIET_MS;
                            BotShared::BotChat::SayToParty(me, Phrases::Say("supply.failed", tags, {}));
                            return false;
                        }
                        state.pauseUntilMs = now + RETRY_MS;
                        return true;
                }
            }

            if (shortKinds.empty())
            {
                sProvisionDesk.Leave(leader->GetObjectGuid(), me->GetObjectGuid());
                state.queued = false;
                return false;
            }
            switch (sProvisionDesk.Want(me, leader, shortKinds, tags))
            {
                case Provision::LeaderStep::Refused:
                    state.leaderRefused = true;
                    state.queued = false;
                    return false;
                case Provision::LeaderStep::Quiet:
                    state.leaderQuietUntilMs = now + Provision::Desk::QUIET_MS;
                    state.queued = false;
                    return false;
                case Provision::LeaderStep::Wait:
                    return false;
                case Provision::LeaderStep::MyTurn:
                    break;
            }
            if (now < state.pauseUntilMs || hands.active)
                return true;
            if (!Errands::MayRun(me, leader) || leader->IsInCombat())
                return false;
            if (!WalkUpTo(me, state, leader, now) || leader->IsMoving())
                return true;
            state.heldBefore[0] = Provisions::Held(me, Kind::Drink);
            state.heldBefore[1] = Provisions::Held(me, Kind::Food);
            Trade::Begin(me, leader, {}, hands);
            state.trading = true;
            return true;
        }
    }

    bool Update(Player* me, AskProvisionsState& state, Player* leader, std::vector<Player*> const& mates,
                Trade::Hands& hands, std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        bool const idle = !state.orderId && !state.trading && !state.queued;
        if (idle && now < state.nextCheckMs)
            return false;
        state.nextCheckMs = now + CHECK_MS;

        // The giver: a mage bot of the party (the one of its order first), else a mage leader.
        Player* giverBot = nullptr;
        Provision::Kinds kinds;
        for (Player* mate : mates)
        {
            if (!mate || mate == me || !mate->IsAlive())
                continue;
            bool const ordered = state.orderId && mate->GetObjectGuid() == state.giver;
            if (!ordered && (state.orderId || Quiet(state.giverQuietUntilMs, mate->GetObjectGuid(), now)))
                continue;
            Provision::Kinds const made = MadeFor(mate, me, true);
            if (made.empty() && !ordered)
                continue;
            giverBot = mate;
            kinds = made;
            break;
        }
        bool const leaderGives = !giverBot && leader && !leader->IsBot() && !state.leaderRefused &&
                                 (!state.leaderQuietUntilMs || now >= state.leaderQuietUntilMs);
        Provision::Kinds const leaderKinds = leaderGives ? MadeFor(leader, me, false) : Provision::Kinds();
        SetProvided(me, giverBot ? kinds : leaderKinds);

        if (state.orderId)
            return FollowOrder(me, state, tags, now);
        if (leader && (state.trading || state.queued))
        {
            if (!leaderGives && !state.trading)
            {
                sProvisionDesk.Leave(leader->GetObjectGuid(), me->GetObjectGuid());
                state.queued = false;
                return false;
            }
            return FollowLeader(me, state, leader, hands, tags, now);
        }

        Provision::Kinds const shortKinds = ShortOf(me, giverBot ? kinds : leaderKinds);
        if (shortKinds.empty() || now < state.nextAskMs)
            return false;
        if (!Errands::MayRun(me, leader) || (leader && leader->IsInCombat()))
            return false;

        if (giverBot)
        {
            state.orderId = sProvisionDesk.Post(giverBot->GetObjectGuid(), me->GetObjectGuid(), shortKinds);
            if (!state.orderId)
                return false;
            state.giver = giverBot->GetObjectGuid();
            state.orderMs = now;
            state.orderKinds = shortKinds;
            state.asked = false;
            Trip::Approach::Begin(state.approach, now);
            return FollowOrder(me, state, tags, now);
        }
        if (!leaderKinds.empty())
        {
            state.queued = true;
            return FollowLeader(me, state, leader, hands, tags, now);
        }
        return false;
    }

    void Pause(Player* me, AskProvisionsState& state, Trade::Hands& hands)
    {
        if (!state.trading)
            return;
        Trade::Cancel(me, hands);
        state.trading = false;
        ++state.failures;
        state.pauseUntilMs = WorldTimer::getMSTime() + RETRY_MS;
    }
}
}
