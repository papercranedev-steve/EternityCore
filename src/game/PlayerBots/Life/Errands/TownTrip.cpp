#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/NpcVisit.h"
#include "Life/Requests/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/ClassTraining.h"
#include "Shared/NpcDirectory.h"
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

        void Enter(TownTripState& state, Phase phase, uint32 now)
        {
            state.phase = phase;
            state.phaseSinceMs = now;
            state.nextMoveMs = 0;
            state.arrivedMs = 0;
        }

        void Reset(Player* me, TownTripState& state, uint32 now, bool walking)
        {
            state.stops.clear();
            state.next = 0;
            state.requestId = 0;
            Enter(state, Phase::Idle, now);
            state.nextCheckMs = now + CHECK_MS;
            if (walking)
                Errands::StopWalk(me);
        }

        // The party is settled in town: the leader rests (city, inn) and stands.
        bool LeaderSettled(Player const* leader)
        {
            return leader->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) && !leader->IsMoving() && !leader->IsMounted() &&
                   !leader->IsInCombat();
        }

        bool LeaderLeft(Player const* leader, TownTripState const& state)
        {
            if (leader->GetMapId() != state.leaderMap || leader->IsMounted() || leader->IsInCombat())
                return true;
            float const dx = leader->GetPositionX() - state.leaderX;
            float const dy = leader->GetPositionY() - state.leaderY;
            return dx * dx + dy * dy > LEADER_LEAVE_YD * LEADER_LEAVE_YD;
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

        bool TryAsk(Player* me, TownTripState& state, Player* leader, std::vector<INpcTask*> const& tasks,
                    std::vector<std::string> const& tags, uint32 now)
        {
            if (!LeaderSettled(leader) || leader->GetMapId() != me->GetMapId())
                return false;
            if (!sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Errand))
                return false;

            state.stops.clear();
            for (INpcTask* task : tasks)
            {
                if (!task || !task->NpcFlags() || !task->WorthTrip(me))
                    continue;
                if (Creature* npc = FindStop(me, leader, *task))
                    state.stops.push_back({ npc->GetObjectGuid(), task });
            }
            if (state.stops.empty())
                return false;

            Phrases::Vars vars;
            vars["places"] = PlacesOf(state.stops);
            vars["leader"] = leader->GetName();
            uint32 const trainerCost = BotShared::ClassTraining::CostOfAvailable(me);
            vars["gold"] = trainerCost ? Phrases::Money(trainerCost) : std::string();
            std::string const text = Phrases::Say("errand.ask", tags, vars);

            Requests::Ask ask;
            ask.intent = Requests::Intent::Errand;
            ask.text = text;
            ask.options = {
                { "yes", Phrases::Word("errand.opt.yes", "yes") },
                { "no",  Phrases::Word("errand.opt.no", "no") },
            };
            ask.fallback = "yes";   // nobody minds: it goes
            state.requestId = sRequestBoard.Open(me, leader, ask);
            if (!state.requestId)
            {
                state.stops.clear();
                return false;
            }
            BotShared::BotChat::SayToParty(me, text);
            Enter(state, Phase::Asking, now);
            return true;
        }

        void Leave(Player* me, TownTripState& state, Player const* leader, std::vector<std::string> const& tags, uint32 now)
        {
            state.leaderMap = leader->GetMapId();
            state.leaderX = leader->GetPositionX();
            state.leaderY = leader->GetPositionY();
            state.next = 0;
            Say(me, "errand.go", tags);
            Enter(state, Phase::Going, now);
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

                if (!me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) && now >= state.nextMoveMs)
                {
                    state.nextMoveMs = now + REPATH_MS;
                    me->GetMotionMaster()->MovePoint(0, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(),
                                                     MOVE_PATHFINDING | MOVE_RUN_MODE);
                }
                return true;
            }
            return false;
        }

        // False: back (or given up: the follow takes over).
        bool UpdateReturning(Player* me, TownTripState& state, Player const* leader, uint32 now)
        {
            if (me->IsWithinDist(leader, BACK_YD, false) || WorldTimer::getMSTimeDiff(state.phaseSinceMs, now) > RETURN_GIVE_UP_MS)
                return false;
            if (!me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) && now >= state.nextMoveMs)
            {
                state.nextMoveMs = now + REPATH_MS;
                me->GetMotionMaster()->MovePoint(0, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(),
                                                 MOVE_PATHFINDING | MOVE_RUN_MODE);
            }
            return true;
        }
    }

    void Stop(Player* me, TownTripState& state)
    {
        if (state.phase == Phase::Idle)
            return;
        if (state.requestId)
            sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
        bool const walking = state.phase == Phase::Going || state.phase == Phase::Returning;
        Reset(me, state, WorldTimer::getMSTime(), walking);
    }

    bool Update(Player* me, TownTripState& state, Player* leader,
                std::vector<INpcTask*> const& tasks, std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (!leader || !Errands::MayRun(me, leader))
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
                if (!LeaderSettled(leader))
                {
                    Stop(me, state);
                    return false;
                }
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
                bool const expired = status.state == Requests::State::Expired;
                if (status.answer == "yes")
                {
                    // (no answer counts as a yes: it goes, and the trip's cooldown follows)
                    sRequestBoard.Close(state.requestId, Requests::Outcome::Accepted);
                    state.requestId = 0;
                    Leave(me, state, leader, tags, now);
                    return true;
                }
                sRequestBoard.Close(state.requestId, expired ? Requests::Outcome::Expired : Requests::Outcome::Declined);
                Say(me, "errand.declined", tags);
                Reset(me, state, now, false);
                return false;
            }
            case Phase::Going:
            {
                if (LeaderLeft(leader, state))
                {
                    Say(me, "errand.abort", tags);
                    Reset(me, state, now, true);
                    state.cooldownUntilMs = now + ABORT_COOLDOWN_MS;
                    return false;
                }
                if (UpdateGoing(me, state, now))
                    return true;
                Enter(state, Phase::Returning, now);
                return true;
            }
            case Phase::Returning:
            {
                if (UpdateReturning(me, state, leader, now))
                    return true;
                if (me->IsWithinDist(leader, BACK_YD * 2, false))
                    Say(me, "errand.back", tags);
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s is back from its town trip.", me->GetName());
                Reset(me, state, now, true);
                state.cooldownUntilMs = now + COOLDOWN_MS;
                return false;
            }
        }
        return false;
    }
}
}
