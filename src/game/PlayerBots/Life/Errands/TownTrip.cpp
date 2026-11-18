#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/NpcVisit.h"
#include "Life/Requests/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/ClassTraining.h"
#include "Shared/NpcDirectory.h"
#include "Shared/TownGates.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Timer.h"
#include "Log.h"

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
            state.arrivedMs = 0;
        }

        bool Walking(Phase phase)
        {
            return phase == Phase::Going || phase == Phase::Returning || phase == Phase::ToGate;
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

        // The nearest NPC (to the leader) out of NpcVisit's reach the task has business with.
        Creature* FindStop(Player* me, Player const* leader, INpcTask& task)
        {
            Map* map = me->GetMap();
            for (BotShared::NpcDirectory::ServiceNpc const& n : BotShared::NpcDirectory::Near(
                     map->GetId(), leader->GetPositionX(), leader->GetPositionY(), TRIP_YD, task.NpcFlags()))
            {
                Creature* npc = map->GetCreature(BotShared::NpcDirectory::GuidOf(n));
                if (!npc || !npc->IsAlive() || npc->IsInCombat() || me->IsHostileTo(npc))
                    continue;
                if (leader->IsWithinDist(npc, NpcVisit::ANCHOR_YD, false))
                    continue;   // NpcVisit's
                if (!task.Wants(me, npc))
                    continue;
                if (!Errands::WalkIsSafe(me, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), MAX_PATH_DETOUR))
                    continue;
                return npc;
            }
            return nullptr;
        }

        std::string PlacesOf(std::vector<TownTripStop> const& stops)
        {
            std::string places;
            std::string const joiner = Phrases::Word("place.and", "/");
            for (size_t i = 0; i < stops.size(); ++i)
            {
                if (i)
                    places += joiner;
                std::string const name = stops[i].task->Name();
                places += Phrases::Word("place." + name, name);
            }
            return places;
        }

        void TryAsk(Player* me, TownTripState& state, Player* leader, std::vector<INpcTask*> const& tasks,
                    std::vector<std::string> const& tags, uint32 now)
        {
            if (!LeaderSettled(leader) || leader->GetMapId() != me->GetMapId())
                return;
            if (!sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Errand))
                return;

            state.stops.clear();
            for (INpcTask* task : tasks)
            {
                if (!task || !task->NpcFlags() || !task->WorthTrip(me))
                    continue;
                if (Creature* npc = FindStop(me, leader, *task))
                    state.stops.push_back({ npc->GetObjectGuid(), task });
            }
            if (state.stops.empty())
                return;

            Phrases::Vars vars;
            vars["places"] = PlacesOf(state.stops);
            vars["leader"] = leader->GetName();
            uint32 const trainerCost = BotShared::ClassTraining::CostOfAvailable(me);
            vars["gold"] = trainerCost ? Phrases::Money(trainerCost) : std::string();

            Requests::Ask ask;
            ask.intent = Requests::Intent::Errand;
            ask.text = Phrases::Say("errand.ask", tags, vars);
            ask.options = {
                { "yes", Phrases::Word("errand.opt.yes", "yes") },
                { "no",  Phrases::Word("errand.opt.no", "no") },
            };
            ask.fallback = "yes";   // nobody minds: it goes
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
                TownTripStop const& stop = state.stops[state.next];
                Creature* npc = me->GetMap()->GetCreature(stop.npc);
                if (!npc || !npc->IsAlive() || npc->IsInCombat() ||
                    (!state.arrivedMs && WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) > STOP_GIVE_UP_MS))
                {
                    ++state.next;
                    Enter(state, Phase::Going, now);
                    continue;
                }

                if (me->CanInteractWithNPC(npc, 0))
                {
                    if (!state.arrivedMs)
                    {
                        if (!me->IsStopped())
                            me->StopMoving();
                        me->SetFacingToObject(npc);
                        state.arrivedMs = now;
                    }
                    if (WorldTimer::getMSTimeDiff(state.arrivedMs, now) < NpcVisit::DWELL_MS)
                        return true;
                    if (stop.task->Wants(me, npc))
                        stop.task->Do(me, npc);
                    ++state.next;
                    Enter(state, Phase::Going, now);
                    continue;
                }

                MoveTo(me, state, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), now);
                return true;
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
    }

    void Stop(Player* me, TownTripState& state)
    {
        if (state.phase == Phase::Idle)
            return;
        if (state.requestId)
            sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
        bool const tripMade = state.phase != Phase::Asking;
        Reset(me, state, WorldTimer::getMSTime(), tripMade);
    }

    bool Update(Player* me, TownTripState& state, Player* leader,
                std::vector<INpcTask*> const& tasks, std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        bool const beforeTrip = state.phase == Phase::Idle || state.phase == Phase::Asking;
        // (out on the trip the leader may ride off: the bot is not his anchor-bound follower then)
        if (!leader || !Errands::MayRun(me, beforeTrip ? leader : nullptr) ||
            (state.phase != Phase::Idle && leader->GetMapId() != state.leaderMap))
        {
            Stop(me, state);
            return false;
        }

        switch (state.phase)
        {
            case Phase::Idle:
            {
                if (now < state.nextCheckMs || now < state.cooldownUntilMs)
                    return false;
                state.nextCheckMs = now + CHECK_MS;
                TryAsk(me, state, leader, tasks, tags, now);
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
                if (status.answer == "yes")
                {
                    // (no answer counts as a yes: it goes)
                    sRequestBoard.Close(state.requestId, Requests::Outcome::Accepted);
                    state.requestId = 0;
                    state.next = 0;
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
                // The follow walks it to him (no teleport for companions on the same map).
                if (me->IsWithinDist(leader, BACK_YD, false))
                {
                    Say(me, "errand.back", tags);
                    Reset(me, state, now, true);
                }
                else if (WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) > COME_GIVE_UP_MS)
                    Reset(me, state, now, true);
                return false;
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
