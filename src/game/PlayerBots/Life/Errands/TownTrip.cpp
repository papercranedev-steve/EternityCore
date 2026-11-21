#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/NpcVisit.h"
#include "Life/Requests/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/ClassTraining.h"
#include "Life/Errands/Trip/RouteOrder.h"
#include "Shared/TownGates.h"
#include "Shared/Trade.h"
#include "GameObject.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <algorithm>

namespace Life
{
namespace TownTrip
{
    namespace
    {
        using Phase = TownTripState::Phase;

        void Say(Player* me, std::string const& key, std::vector<std::string> const& tags, Phrases::Vars const& vars = {})
        {
            BotShared::BotChat::SayToParty(me, Phrases::Say(key, tags, vars));
        }

        void Whisper(Player* me, Player* leader, std::string const& key, std::vector<std::string> const& tags,
                     Phrases::Vars const& vars = {})
        {
            BotShared::BotChat::WhisperTo(me, leader, Phrases::Say(key, tags, vars));
        }

        void Enter(TownTripState& state, Phase phase, uint32 now)
        {
            state.phase = phase;
            state.phaseSinceMs = now;
            state.nextMoveMs = 0;
            state.browseMs = 0;
            state.stopGiveUpMs = 0;
            Trip::Approach::Begin(state.leg, now);
        }

        bool Walking(Phase phase)
        {
            return phase == Phase::Going || phase == Phase::Returning || phase == Phase::Coming || phase == Phase::ToGate;
        }

        void Reset(Player* me, TownTripState& state, uint32 now, bool cooldown)
        {
            if (Walking(state.phase))
                Errands::StopWalk(me);
            state.stops.clear();
            state.next = 0;
            state.requestId = 0;
            state.hasGate = false;
            state.waitName.clear();
            Enter(state, Phase::Idle, now);
            state.nextCheckMs = now + CHECK_MS;
            if (cooldown)
                state.cooldownUntilMs = now + COOLDOWN_MS;
        }

        // The party is settled in town: the leader rests (city, inn) and stands.
        bool LeaderSettled(Player const* leader)
        {
            return leader->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) && !leader->IsMoving() && !leader->IsMounted() &&
                   !leader->IsInCombat();
        }

        void MoveTo(Player* me, TownTripState& state, float x, float y, float z, uint32 now)
        {
            if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) || now < state.nextMoveMs)
                return;
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        }

        // How long it stands at a stop where it handled `things` (Trip::Browse).
        uint32 BrowseMs(Trip::Browse const& browse, uint32 things)
        {
            uint32 ms = urand(browse.baseMinMs, std::max(browse.baseMinMs, browse.baseMaxMs));
            for (uint32 i = 0; i < things && ms < browse.capMs; ++i)
                ms += urand(browse.perThingMs * 7 / 10, browse.perThingMs * 13 / 10);
            return std::min(ms, std::max(browse.capMs, browse.baseMinMs));
        }

        // "拍卖行、护甲商和训练师": the named stops in route order, each word once.
        std::string PlacesOf(std::vector<Trip::TripStop> const& stops)
        {
            std::vector<std::string> words;
            for (Trip::TripStop const& s : stops)
            {
                if (s.placeKey.empty())
                    continue;
                std::string const word = Phrases::Word(s.placeKey, Phrases::Word("place.vendor", s.action->Name()));
                if (std::find(words.begin(), words.end(), word) == words.end())
                    words.push_back(word);
            }
            std::string const comma = Phrases::Word("place.comma", ", ");
            std::string const last = Phrases::Word("place.and", " / ");
            std::string places;
            for (size_t i = 0; i < words.size(); ++i)
            {
                if (i)
                    places += i + 1 == words.size() ? last : comma;
                places += words[i];
            }
            return places;
        }

        // What the trip is for ("补点东西，顺便学新技能"), from the planners on it.
        std::string PurposesOf(std::vector<Trip::ITripPlanner*> const& going)
        {
            std::string why;
            for (Trip::ITripPlanner const* planner : going)
            {
                char const* key = planner->PurposeKey();
                if (!key)
                    continue;
                if (!why.empty())
                    why += Phrases::Word("errand.why.and", ", and ");
                why += Phrases::Word(key, planner->Name());
            }
            return why;
        }

        void TryAsk(Player* me, TownTripState& state, Player* leader, std::vector<Trip::ITripPlanner*> const& planners,
                    std::vector<std::string> const& tags, uint32 now)
        {
            if (!LeaderSettled(leader) || leader->GetMapId() != me->GetMapId())
                return;
            if (!sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Errand))
                return;
            if (std::none_of(planners.begin(), planners.end(), [me](Trip::ITripPlanner* p) { return p && p->Worth(me); }))
                return;

            Trip::TripPlanStops plan;
            for (size_t i = 0; i < planners.size(); ++i)
                if (planners[i])
                {
                    size_t const before = plan.stops.size();
                    planners[i]->PlanStops(me, leader, plan);
                    for (size_t s = before; s < plan.stops.size(); ++s)
                        plan.stops[s].planner = int(i);
                }
            if (!Trip::RouteOrder::Order(me, plan))
                return;
            state.stops = std::move(plan.stops);
            // The planners with a stop left on the route (a dropped stop is not in the question).
            std::vector<Trip::ITripPlanner*> going;
            for (size_t i = 0; i < planners.size(); ++i)
                if (std::any_of(state.stops.begin(), state.stops.end(), [i](Trip::TripStop const& s) { return s.planner == int(i); }))
                    going.push_back(planners[i]);

            Phrases::Vars vars;
            vars["places"] = PlacesOf(state.stops);
            vars["why"] = PurposesOf(going);
            vars["leader"] = leader->GetName();
            uint32 const trainerCost = BotShared::ClassTraining::CostOfAvailable(me);
            vars["gold"] = trainerCost ? Phrases::Money(trainerCost) : std::string();

            Requests::Ask ask;
            ask.intent = Requests::Intent::Errand;
            ask.text = Phrases::Say("trip.ask", tags, vars);
            uint32 shortfall = 0;
            for (Trip::ITripPlanner* planner : going)
            {
                planner->Notes(me, leader, ask.notes);
                shortfall += planner->Shortfall(me);
            }
            if (shortfall && !state.paysOwnWay)
                ask.options = {
                    { "pay", Phrases::Word("errand.opt.pay", "pay") },
                    { "yes", Phrases::Word("errand.opt.nopay", "yes") },
                    { "no",  Phrases::Word("errand.opt.no", "no") },
                };
            else
                ask.options = {
                    { "yes", Phrases::Word("errand.opt.yes", "yes") },
                    { "no",  Phrases::Word("errand.opt.no", "no") },
                };
            ask.fallback = "yes";   // nobody minds: it goes (without his money)
            state.requestId = sRequestBoard.Open(me, leader, ask);
            if (!state.requestId)
            {
                state.stops.clear();
                return;
            }
            BotShared::BotChat::SayToParty(me, ask.text);
            state.leaderMap = leader->GetMapId();
            Enter(state, Phase::Asking, now);
        }

        // Walks to (or stands at) the current stop. False: the stops are done.
        bool UpdateGoing(Player* me, TownTripState& state, uint32 now)
        {
            while (state.next < state.stops.size())
            {
                Trip::TripStop const& stop = state.stops[state.next];
                Trip::Destination dest;
                dest.target = stop.target;
                if (!state.stopGiveUpMs)
                {
                    float const dx = stop.x - me->GetPositionX();
                    float const dy = stop.y - me->GetPositionY();
                    bool const farStop = dx * dx + dy * dy > TRIP_YD * TRIP_YD;
                    state.stopGiveUpMs = farStop || !Trip::Approach::TargetOf(me, stop.target) ? FAR_STOP_GIVE_UP_MS : STOP_GIVE_UP_MS;
                }
                uint32 const giveUpMs = state.stopGiveUpMs;
                // An NPC whose grid is not loaded yet (a trainer across a capital): its spawn
                // point first; on the way it appears.
                if (!stop.target.IsEmpty() && !Trip::Approach::TargetOf(me, stop.target))
                {
                    Trip::Destination spawn;
                    spawn.x = stop.x;
                    spawn.y = stop.y;
                    spawn.z = stop.z;
                    spawn.reachYd = SPAWN_REACH_YD;
                    Trip::ApproachResult const result = Trip::Approach::Update(me, spawn, state.leg, now, giveUpMs);
                    if (result == Trip::ApproachResult::Walking)
                        return true;
                    // There and still nobody (dead, walked off), or the walk failed.
                    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s skips its %s stop: %s.", me->GetName(), stop.action->Name(),
                        result == Trip::ApproachResult::Arrived ? "nobody at the spawn point" : "could not walk to the spawn point in time");
                    ++state.next;
                    Enter(state, Phase::Going, now);
                    continue;
                }
                switch (Trip::Approach::Update(me, dest, state.leg, now, giveUpMs))
                {
                    case Trip::ApproachResult::Walking:
                        return true;
                    case Trip::ApproachResult::Failed:
                        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s skips its %s stop: could not reach the NPC in time.",
                            me->GetName(), stop.action->Name());
                        ++state.next;
                        Enter(state, Phase::Going, now);
                        continue;
                    case Trip::ApproachResult::Arrived:
                        break;
                }

                // Arrived: the business at once, then it stands there longer the more it did
                // (gone while it stands: the next stop).
                WorldObject* target = Trip::Approach::TargetOf(me, stop.target);
                if (!state.browseMs)
                {
                    uint32 const things = target ? stop.action->Do(me, target) : 0;
                    state.browseMs = std::max<uint32>(BrowseMs(stop.action->BrowseTime(), things), 1);
                }
                if (target && WorldTimer::getMSTimeDiff(state.leg.arrivedMs, now) < state.browseMs)
                    return true;
                ++state.next;
                Enter(state, Phase::Going, now);
            }
            return false;
        }

        // The stops are done: walk back, or ask the far leader where to meet.
        void Finish(Player* me, TownTripState& state, Player* leader, std::vector<std::string> const& tags, uint32 now)
        {
            Errands::StopWalk(me);
            if (me->IsWithinDist(leader, NEAR_YD, false))
            {
                Enter(state, Phase::Returning, now);
                return;
            }

            BotShared::TownGates::Gate gate;
            state.hasGate = BotShared::TownGates::Nearest(me->GetMapId(), me->GetZoneId(), me->GetPositionX(), me->GetPositionY(), gate);
            if (state.hasGate)
            {
                state.waitX = gate.x;
                state.waitY = gate.y;
                state.waitZ = gate.z;
                state.waitName = gate.name.empty() ? Phrases::Word("place.gate", "gate") : gate.name;
            }
            else
            {
                state.waitX = me->GetPositionX();
                state.waitY = me->GetPositionY();
                state.waitZ = me->GetPositionZ();
                state.waitName.clear();
            }

            Phrases::Vars vars;
            vars["gate"] = state.hasGate ? state.waitName : Phrases::Word("place.here", "here");
            Requests::Ask ask;
            ask.intent = Requests::Intent::Rendezvous;
            ask.text = Phrases::Say("meet.ask", tags, vars);
            ask.options.push_back({ "come", Phrases::Format(Phrases::Word("meet.opt.come", "come"), vars) });
            if (state.hasGate)
                ask.options.push_back({ "gate", Phrases::Format(Phrases::Word("meet.opt.gate", "gate"), vars) });
            else
                ask.options.push_back({ "here", Phrases::Word("meet.opt.here", "here") });
            ask.fallback = "come";
            state.requestId = sRequestBoard.Open(me, leader, ask);
            if (!state.requestId)
            {
                // (another question open: just come back)
                Enter(state, Phase::Coming, now);
                return;
            }
            BotShared::BotChat::WhisperTo(me, leader, ask.text);
            Enter(state, Phase::Reporting, now);
        }

        bool UpdateReporting(Player* me, TownTripState& state, Player* leader, std::vector<std::string> const& tags, uint32 now)
        {
            // He came by himself.
            if (me->IsWithinDist(leader, MEET_YD, false))
            {
                sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
                state.requestId = 0;
                Say(me, "errand.back", tags);
                Reset(me, state, now, true);
                return false;
            }
            Requests::Status const status = sRequestBoard.Poll(state.requestId);
            if (status.state == Requests::State::Open)
                return true;
            sRequestBoard.Close(state.requestId, status.state == Requests::State::Answered ?
                                Requests::Outcome::Accepted : Requests::Outcome::Expired);
            state.requestId = 0;

            Phrases::Vars vars;
            vars["gate"] = state.waitName;
            if (status.answer == "gate" && state.hasGate)
            {
                Whisper(me, leader, "meet.gate", tags, vars);
                Enter(state, Phase::ToGate, now);
                return true;
            }
            if (status.answer == "here")
            {
                Whisper(me, leader, "meet.here", tags, vars);
                Enter(state, Phase::Waiting, now);
                return true;
            }
            Whisper(me, leader, "meet.come", tags, vars);
            Enter(state, Phase::Coming, now);
            return false;
        }

        // "pay": up to the leader, a trade for his money. True while it goes on; false: go.
        bool UpdateCollecting(Player* me, TownTripState& state, Player* leader, BotShared::Trade::Hands& hands, uint32 now)
        {
            if (WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) > COLLECT_GIVE_UP_MS)
            {
                if (state.tradeBegun)
                    BotShared::Trade::Cancel(me, hands);
                state.tradeBegun = false;
                return false;
            }
            if (!state.tradeBegun)
            {
                if (hands.active)
                    return true;    // (another trade first)
                if (!me->IsWithinDistInMap(leader, TRADE_YD))
                {
                    MoveTo(me, state, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), now);
                    return true;
                }
                if (me->IsMoving())
                {
                    Errands::StopWalk(me);
                    return true;
                }
                if (leader->IsMoving())
                    return true;
                BotShared::Trade::Begin(me, leader, {}, hands);
                state.tradeBegun = true;
                return true;
            }
            switch (BotShared::Trade::Update(me, hands))
            {
                case BotShared::Trade::Result::Busy:
                    return true;
                case BotShared::Trade::Result::Completed:
                case BotShared::Trade::Result::Declined:
                    state.tradeBegun = false;
                    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s got %u copper from %s for its trip.", me->GetName(),
                             me->GetMoney() > state.moneyBefore ? me->GetMoney() - state.moneyBefore : 0, leader->GetName());
                    return false;
                default:
                    state.tradeBegun = false;
                    return ++state.tradeFailures <= MAX_TRADE_RETRIES;
            }
        }
    }

    void Stop(Player* me, TownTripState& state, BotShared::Trade::Hands& hands)
    {
        if (state.phase == Phase::Idle)
            return;
        if (state.phase == Phase::Collecting && state.tradeBegun)
            BotShared::Trade::Cancel(me, hands);
        state.tradeBegun = false;
        if (state.requestId)
            sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
        bool const tripMade = state.phase != Phase::Asking;
        Reset(me, state, WorldTimer::getMSTime(), tripMade);
    }

    uint32 TakePaid(TownTripState& state)
    {
        uint32 const paid = state.paidCopper;
        state.paidCopper = 0;
        return paid;
    }

    bool Update(Player* me, TownTripState& state, Player* leader,
                std::vector<Trip::ITripPlanner*> const& planners, std::vector<std::string> const& tags,
                BotShared::Trade::Hands& hands)
    {
        uint32 const now = WorldTimer::getMSTime();
        bool const beforeTrip = state.phase == Phase::Idle || state.phase == Phase::Asking;
        // (out on the trip the leader may ride off: the bot is not his anchor-bound follower then)
        // (asking: it follows meanwhile and may sit down to drink; that does not withdraw the
        // question, the popup stayed on the leader's screen then vanished)
        if (!leader || !Errands::MayRun(me, beforeTrip ? leader : nullptr, state.phase == Phase::Asking) ||
            (state.phase != Phase::Idle && leader->GetMapId() != state.leaderMap))
        {
            Stop(me, state, hands);
            return false;
        }

        switch (state.phase)
        {
            case Phase::Idle:
            {
                if (now < state.nextCheckMs || now < state.cooldownUntilMs)
                    return false;
                state.nextCheckMs = now + CHECK_MS;
                TryAsk(me, state, leader, planners, tags, now);
                return false;   // it keeps following while it asks
            }
            case Phase::Asking:
            {
                Requests::Status const status = sRequestBoard.Poll(state.requestId);
                switch (status.state)
                {
                    case Requests::State::Open:
                        return false;
                    case Requests::State::None:
                        Reset(me, state, now, false);
                        return false;
                    default:
                        break;
                }
                if (status.answer == "yes" || status.answer == "pay")
                {
                    // (no answer counts as a yes: it goes)
                    sRequestBoard.Close(state.requestId, Requests::Outcome::Accepted);
                    state.requestId = 0;
                    state.next = 0;
                    // Sat down while it waited (a drink): it gets up and goes, as the leader
                    // said (in town; it drinks again when back).
                    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
                        me->SetStandState(UNIT_STAND_STATE_STAND);
                    if (status.answer == "pay")
                    {
                        state.tradeBegun = false;
                        state.tradeFailures = 0;
                        state.moneyBefore = me->GetMoney();
                        Enter(state, Phase::Collecting, now);
                        return true;
                    }
                    Say(me, "errand.go", tags);
                    Enter(state, Phase::Going, now);
                    return true;
                }
                sRequestBoard.Close(state.requestId, status.state == Requests::State::Expired ?
                                    Requests::Outcome::Expired : Requests::Outcome::Declined);
                Say(me, "errand.declined", tags);
                Reset(me, state, now, false);
                return false;
            }
            case Phase::Collecting:
            {
                if (UpdateCollecting(me, state, leader, hands, now))
                    return true;
                if (me->GetMoney() > state.moneyBefore)
                    state.paidCopper += me->GetMoney() - state.moneyBefore;
                Say(me, me->GetMoney() > state.moneyBefore ? "errand.paid" : "errand.go", tags);
                Enter(state, Phase::Going, now);
                return true;
            }
            case Phase::Going:
            {
                if (UpdateGoing(me, state, now))
                    return true;
                Finish(me, state, leader, tags, now);
                return state.phase != Phase::Coming;
            }
            case Phase::Returning:
            {
                if (!me->IsWithinDist(leader, BACK_YD, false) &&
                    WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) <= RETURN_GIVE_UP_MS)
                {
                    MoveTo(me, state, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), now);
                    return true;
                }
                Say(me, "errand.back", tags);
                Reset(me, state, now, true);
                return false;
            }
            case Phase::Reporting:
                return UpdateReporting(me, state, leader, tags, now);
            case Phase::Coming:
            {
                // It walks back to him itself (pathfinding, repathed as he moves). Left to the
                // follow it was "far behind" across a capital (beyond Summon::FAR_YD) and the
                // far travel took it: invisible, 100 yd hops on a straight line (over the canals).
                // That travel stays the fallback once it gives up.
                if (me->IsWithinDist(leader, BACK_YD, false))
                {
                    Say(me, "errand.back", tags);
                    Reset(me, state, now, true);
                    return false;
                }
                if (WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) > COME_GIVE_UP_MS)
                {
                    Reset(me, state, now, true);
                    return false;
                }
                MoveTo(me, state, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), now);
                return true;
            }
            case Phase::ToGate:
            {
                float const dx = me->GetPositionX() - state.waitX;
                float const dy = me->GetPositionY() - state.waitY;
                bool const there = dx * dx + dy * dy <= AT_GATE_YD * AT_GATE_YD;
                if (!there && WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) <= GATE_GIVE_UP_MS)
                {
                    MoveTo(me, state, state.waitX, state.waitY, state.waitZ, now);
                    return true;
                }
                Errands::StopWalk(me);
                Whisper(me, leader, "meet.arrived", tags, { { "gate", state.waitName } });
                Enter(state, Phase::Waiting, now);
                return true;
            }
            case Phase::Waiting:
            {
                if (!me->IsWithinDist(leader, MEET_YD, false))
                    return true;    // stands there (no follow)
                Say(me, "errand.back", tags);
                Reset(me, state, now, true);
                return false;
            }
        }
        return false;
    }
}
}
