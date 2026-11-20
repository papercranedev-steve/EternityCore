#include "Life/Errands/GiveProvisions.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Requests/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/Crafting.h"
#include "Shared/Provisions.h"
#include "Shared/Trade.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Life
{
namespace GiveProvisions
{
    namespace
    {
        using BotShared::Provisions::Kind;
        namespace Provisions = BotShared::Provisions;
        namespace Trade = BotShared::Trade;

        Provision::Kinds const ALL_KINDS = { Kind::Drink, Kind::Food, Kind::Healthstone };

        // What a gift to `taker` holds: the kinds it uses that the bot can make for it (a
        // healthstone only when it has none: one per player).
        Provision::Kinds GiftKinds(Player const* me, Player const* taker)
        {
            Provision::Kinds kinds;
            for (Kind kind : ALL_KINDS)
            {
                // (a mage or warlock leader makes his own)
                if (!Provisions::Uses(taker, kind) || Provisions::MakesOwn(taker, kind))
                    continue;
                if (kind == Kind::Healthstone && !Provisions::Short(taker, kind))
                    continue;
                if (Provisions::RecipeFor(me, taker, kind))
                    kinds.push_back(kind);
            }
            return kinds;
        }

        bool AnyShort(Player const* taker, Provision::Kinds const& kinds)
        {
            return std::any_of(kinds.begin(), kinds.end(), [taker](Kind kind) { return Provisions::Short(taker, kind); });
        }

        // To a bot that asked: aloud (white /say, as it asked); to the leader: party chat.
        void Tell(Player* me, GiveProvisionsState const& state, char const* key, std::vector<std::string> const& tags,
                  Phrases::Vars const& vars = {})
        {
            std::string const text = Phrases::Say(key, tags, vars);
            if (state.orderId)
                BotShared::BotChat::Say(me, text);
            else
                BotShared::BotChat::SayToParty(me, text);
        }

        bool CastsOwnRecipe(Player* me, uint32 spellId)
        {
            Spell const* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            return spell && spell->m_spellInfo && spell->m_spellInfo->Id == spellId &&
                   spell->getState() != SPELL_STATE_FINISHED;
        }

        bool PowerFor(Player* me, SpellEntry const* recipe)
        {
            uint32 const cost = Spell::CalculatePowerCost(recipe, me, nullptr, nullptr, false);
            return !cost || me->GetPower(Powers(recipe->powerType)) >= cost;
        }

        void Ask(Player* me, GiveProvisionsState& state, Player* leader, Provision::Kinds const& kinds, bool forTrade,
                 std::vector<std::string> const& tags)
        {
            Provision::Kinds food;
            for (Kind kind : kinds)
                if (kind != Kind::Healthstone)
                    food.push_back(kind);

            Phrases::Vars vars;
            vars["leader"] = leader->GetName();
            vars["what"] = Provision::WhatOf(food);

            Requests::Ask ask;
            ask.intent = Requests::Intent::Supply;
            ask.text = Phrases::Say(food.empty() ? "supply.offer.stone" : "supply.offer", tags, vars);
            ask.options = {
                { "yes", Phrases::Word("supply.opt.yes", "yes") },
                { "no", Phrases::Word("supply.opt.no", "no") },
            };
            ask.fallback = "no";
            state.requestId = sRequestBoard.Open(me, leader, ask);
            if (!state.requestId)
                return;
            state.askKinds = kinds;
            state.askedForTrade = forTrade;
            BotShared::BotChat::SayToParty(me, ask.text);
        }

        void StartJob(GiveProvisionsState& state, ObjectGuid taker, Provision::Kinds const& kinds, uint32 orderId, uint32 now)
        {
            state.hasJob = true;
            state.taker = taker;
            state.kinds = kinds;
            state.orderId = orderId;
            state.phase = GivePhase::Make;
            state.phaseMs = state.startMs = now;
            state.failures = 0;
            state.toldMaking = false;
            state.tradeBegun = false;
            state.castingSpell = 0;
        }

        void EndJob(GiveProvisionsState& state, bool done)
        {
            if (state.orderId)
                sProvisionDesk.Finish(state.orderId, done);
            state.hasJob = false;
            state.orderId = 0;
            state.kinds.clear();
            state.taker.Clear();
            state.tradeBegun = false;
            state.castingSpell = 0;
        }

        void PollQuestion(Player* me, GiveProvisionsState& state, Player* leader, std::vector<std::string> const& tags, uint32 now)
        {
            Requests::Status const status = sRequestBoard.Poll(state.requestId);
            if (status.state == Requests::State::Open)
            {
                // (he closed the window he asked about: no more question)
                if (state.askedForTrade && (!leader || Trade::OpenWith(me) != leader))
                {
                    sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
                    state.requestId = 0;
                }
                return;
            }

            uint32 const id = state.requestId;
            state.requestId = 0;
            if (status.state == Requests::State::Answered && status.answer == "yes")
            {
                sRequestBoard.Close(id, Requests::Outcome::Accepted);
                Provision::Kinds const kinds = leader ? GiftKinds(me, leader) : Provision::Kinds();
                if (!kinds.empty())
                    StartJob(state, leader->GetObjectGuid(), kinds, 0, now);
            }
            else if (status.state == Requests::State::Answered)
            {
                sRequestBoard.Close(id, Requests::Outcome::Declined);
                state.declined = true;
                BotShared::BotChat::SayToParty(me, Phrases::Say("supply.declined", tags, {}));
            }
            else
            {
                sRequestBoard.Close(id, Requests::Outcome::Expired);
                if (!state.askedForTrade)
                    state.quietUntilMs = now + QUIET_MS;
            }
        }

        // The products still missing are made one cast after another. True: casting or about
        // to; false: waits for power (the bot rests meanwhile); the phase moves on when ready.
        bool Make(Player* me, GiveProvisionsState& state, Player* taker, std::vector<std::string> const& tags, uint32 now)
        {
            if (state.castingSpell)
            {
                if (CastsOwnRecipe(me, state.castingSpell))
                    return true;
                state.castingSpell = 0;
            }

            SpellEntry const* toMake = nullptr;
            bool held = false;
            for (Kind kind : state.kinds)
            {
                SpellEntry const* recipe = Provisions::RecipeFor(me, taker, kind);
                if (!recipe)
                    continue;
                uint32 const have = me->GetItemCount(Provisions::ProductOf(recipe));
                held = held || have > 0;
                if (!toMake && have < Provisions::GiftCount(kind) && BotShared::Crafting::CastsFromBags(me, recipe))
                    toMake = recipe;
            }

            auto moveOn = [&]()
            {
                if (!held)
                {
                    Tell(me, state, "supply.failed", tags);
                    EndJob(state, false);
                    return false;
                }
                state.phase = GivePhase::Approach;
                state.phaseMs = now;
                Trip::Approach::Begin(state.approach, now);
                return true;
            };

            if (!toMake || WorldTimer::getMSTimeDiff(state.startMs, now) > MAKE_MS)
                return moveOn();

            // A window the leader opened waits for nothing: closed until the gift is made.
            if (Trade::OpenWith(me) == taker)
            {
                Trade::CloseWindow(me);
                if (!state.toldMaking)
                    Tell(me, state, "supply.make", tags);
                state.toldMaking = true;
                return true;
            }
            if (!state.toldMaking && !state.orderId)
                Tell(me, state, "supply.make", tags);
            state.toldMaking = true;

            if (!Errands::MayRun(me, nullptr) || me->IsNonMeleeSpellCasted(false) || !PowerFor(me, toMake))
                return false;
            if (me->IsMoving())
            {
                Errands::StopWalk(me);
                return true;
            }
            SpellCastResult const result = BotShared::Crafting::Craft(me, toMake);
            switch (result)
            {
                case SPELL_CAST_OK:
                    state.castingSpell = toMake->Id;
                    return true;
                case SPELL_FAILED_NO_POWER:
                    return false;
                case SPELL_FAILED_NOT_READY:
                case SPELL_FAILED_SPELL_IN_PROGRESS:
                case SPELL_FAILED_MOVING:
                    return true;
                default:
                    // (bags full ...: what it has)
                    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GiveProvisions] %s cannot cast %u (result %u).",
                             me->GetName(), toMake->Id, uint32(result));
                    return moveOn();
            }
        }

        bool Approach(Player* me, GiveProvisionsState& state, Player* taker, uint32 now)
        {
            if (Trade::OpenWith(me) == taker)
            {
                state.phase = GivePhase::Trade;
                return true;
            }
            if (me->IsWithinDistInMap(taker, TRADE_YD))
            {
                if (me->IsMoving())
                    Errands::StopWalk(me);
                if (!me->IsMoving() && !taker->IsMoving())
                    state.phase = GivePhase::Trade;
                return true;
            }
            if (state.approach.arrivedMs)
                Trip::Approach::Begin(state.approach, now);
            Trip::Destination dest;
            dest.x = taker->GetPositionX();
            dest.y = taker->GetPositionY();
            dest.z = taker->GetPositionZ();
            dest.reachYd = 1.5f;
            Trip::Approach::Update(me, dest, state.approach, now);
            return true;
        }

        bool TradeStep(Player* me, GiveProvisionsState& state, Player* taker, Trade::Hands& hands,
                       std::vector<std::string> const& tags, uint32 now)
        {
            if (!state.tradeBegun)
            {
                if (hands.active)
                    return true;
                std::vector<Item*> items;
                for (Kind kind : state.kinds)
                {
                    SpellEntry const* recipe = Provisions::RecipeFor(me, taker, kind);
                    if (!recipe)
                        continue;
                    uint32 const stacks = kind == Kind::Healthstone ? 1 : MAX_STACKS;
                    for (Item* item : Provisions::StacksFor(me, Provisions::ProductOf(recipe), Provisions::GiftCount(kind), stacks))
                        items.push_back(item);
                }
                if (items.empty())
                {
                    EndJob(state, false);
                    return false;
                }
                Trade::Begin(me, taker, items, hands);
                state.tradeBegun = true;
                if (!state.failures)
                    Tell(me, state, "supply.give", tags);
                return true;
            }

            switch (Trade::Update(me, hands))
            {
                case Trade::Result::Busy:
                    return true;
                case Trade::Result::Completed:
                    EndJob(state, true);
                    return false;
                case Trade::Result::Declined:
                    EndJob(state, false);
                    return false;
                case Trade::Result::NoRoom:
                    Tell(me, state, "supply.noroom", tags);
                    EndJob(state, false);
                    return false;
                case Trade::Result::Failed:
                case Trade::Result::Idle:
                default:
                    state.tradeBegun = false;
                    if (++state.failures > MAX_RETRIES)
                    {
                        Tell(me, state, "supply.failed", tags);
                        EndJob(state, false);
                        return false;
                    }
                    state.phase = GivePhase::Pause;
                    state.phaseMs = now;
                    return true;
            }
        }

        bool RunJob(Player* me, GiveProvisionsState& state, Trade::Hands& hands, std::vector<std::string> const& tags, uint32 now)
        {
            Player* taker = me->GetMap()->GetPlayer(state.taker);
            if (!taker || !taker->IsAlive() || WorldTimer::getMSTimeDiff(state.startMs, now) > JOB_MS)
            {
                if (state.tradeBegun)
                    Trade::Cancel(me, hands);
                EndJob(state, false);
                return false;
            }

            if (state.phase == GivePhase::Pause)
            {
                if (WorldTimer::getMSTimeDiff(state.phaseMs, now) < RETRY_MS)
                    return true;
                state.phase = GivePhase::Approach;
                Trip::Approach::Begin(state.approach, now);
            }
            if (state.phase == GivePhase::Make)
            {
                bool const busy = Make(me, state, taker, tags, now);
                if (!state.hasJob || state.phase == GivePhase::Make)
                    return busy;
            }
            if (state.phase == GivePhase::Approach)
            {
                Approach(me, state, taker, now);
                if (state.phase == GivePhase::Approach)
                    return true;
            }
            return TradeStep(me, state, taker, hands, tags, now);
        }
    }

    bool Update(Player* me, GiveProvisionsState& state, Player* leader, Trade::Hands& hands, std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (state.hasJob)
            return RunJob(me, state, hands, tags, now);

        if (state.requestId)
        {
            PollQuestion(me, state, leader, tags, now);
            return state.hasJob ? RunJob(me, state, hands, tags, now) : false;
        }

        // A trade the leader opened while short: asked about it.
        if (!state.tradeOpenedBy.IsEmpty())
        {
            ObjectGuid const from = state.tradeOpenedBy;
            state.tradeOpenedBy.Clear();
            if (leader && !leader->IsBot() && from == leader->GetObjectGuid() && Trade::OpenWith(me) == leader &&
                sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Supply))
            {
                Provision::Kinds const kinds = GiftKinds(me, leader);
                if (AnyShort(leader, kinds))
                    Ask(me, state, leader, kinds, true, tags);
            }
            if (state.requestId)
                return false;
        }

        // A bot of the party that came and asked.
        Provision::Order order;
        if (Errands::MayRun(me, nullptr) && sProvisionDesk.TakeNext(me->GetObjectGuid(), order))
        {
            Player* taker = me->GetMap()->GetPlayer(order.taker);
            Provision::Kinds kinds;
            if (taker)
                for (Kind kind : order.kinds)
                    if (Provisions::RecipeFor(me, taker, kind))
                        kinds.push_back(kind);
            if (kinds.empty())
                sProvisionDesk.Finish(order.id, false);
            else
            {
                StartJob(state, order.taker, kinds, order.id, now);
                Tell(me, state, "supply.botack", tags);
                return RunJob(me, state, hands, tags, now);
            }
        }

        // On its own: the leader is short.
        if (leader && !leader->IsBot() && !state.declined && now >= state.nextCheckMs &&
            (!state.quietUntilMs || now >= state.quietUntilMs))
        {
            state.nextCheckMs = now + CHECK_MS;
            state.quietUntilMs = 0;
            if (Errands::MayRun(me, leader) && !leader->IsInCombat() && leader->IsAlive() &&
                me->IsWithinDistInMap(leader, ASK_RANGE_YD) &&
                sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Supply))
            {
                Provision::Kinds const kinds = GiftKinds(me, leader);
                if (AnyShort(leader, kinds))
                    Ask(me, state, leader, kinds, false, tags);
            }
        }
        return false;
    }

    void OnTradeOpened(GiveProvisionsState& state, ObjectGuid from)
    {
        state.tradeOpenedBy = from;
    }

    void Pause(Player* me, GiveProvisionsState& state, Trade::Hands& hands)
    {
        if (!state.hasJob)
            return;
        if (state.tradeBegun)
            Trade::Cancel(me, hands);
        if (state.castingSpell && CastsOwnRecipe(me, state.castingSpell))
            me->InterruptSpell(CURRENT_GENERIC_SPELL);
        state.castingSpell = 0;
        state.tradeBegun = false;
        state.phase = GivePhase::Make;
    }
}
}
