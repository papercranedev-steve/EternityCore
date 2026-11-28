#include "Life/Errands/NpcVisit.h"
#include "Life/Errands/ErrandCommon.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Timer.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <list>
#include <vector>

namespace Life
{
namespace NpcVisit
{
    namespace
    {
        bool RecentlyVisited(NpcVisitState const& state, ObjectGuid guid, uint32 now)
        {
            for (auto const& v : state.visited)
                if (v.first == guid && now < v.second)
                    return true;
            return false;
        }

        void MarkVisited(NpcVisitState& state, ObjectGuid guid, uint32 now)
        {
            state.visited.emplace_back(guid, now + RETRY_MS);
            while (state.visited.size() > VISITED_KEEP)
                state.visited.pop_front();
        }

        class FriendlyNpcCheck
        {
        public:
            FriendlyNpcCheck(Player* me, float range) : m_me(me), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && c->GetUInt32Value(UNIT_NPC_FLAGS) && !c->IsHostileTo(m_me) &&
                       !c->IsInCombat() && m_me->IsWithinDist(c, m_range, false);
            }

        private:
            Player* m_me;
            float m_range;
        };

        Creature* Pick(Player* me, NpcVisitState& state, WorldObject const* anchor, INpcTask& task, uint32 now)
        {
            std::list<Creature*> found;
            FriendlyNpcCheck check(me, SEARCH_YD);
            MaNGOS::CreatureListSearcher<FriendlyNpcCheck> searcher(found, check);
            Cell::VisitGridObjects(me, searcher, SEARCH_YD);

            std::vector<Creature*> npcs;
            for (Creature* c : found)
                if (!RecentlyVisited(state, c->GetObjectGuid(), now) && anchor->IsWithinDist(c, ANCHOR_YD, false) && task.Wants(me, c))
                    npcs.push_back(c);
            std::sort(npcs.begin(), npcs.end(), [me](Creature* a, Creature* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (Creature* c : npcs)
            {
                if (me->CanInteractWithNPC(c, 0) ||
                    Errands::WalkIsSafe(me, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ(), MAX_PATH_DETOUR))
                    return c;
                MarkVisited(state, c->GetObjectGuid(), now);
            }
            return nullptr;
        }

        void Finish(Player* me, NpcVisitState& state, uint32 now)
        {
            MarkVisited(state, state.npc, now);
            state.npc.Clear();
            state.arrivedMs = 0;
            Errands::StopWalk(me);
        }
    }

    void Stop(Player* me, NpcVisitState& state)
    {
        state.after = Errands::Beat();
        if (state.npc.IsEmpty())
            return;
        state.npc.Clear();
        state.arrivedMs = 0;
        Errands::StopWalk(me);
    }

    bool Update(Player* me, NpcVisitState& state, Player const* anchor, INpcTask& task)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (!Errands::MayRun(me, anchor))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        if (state.npc.IsEmpty() && Errands::InBeat(me, state.after, anchor, now))
            return true;
        Creature* npc = nullptr;
        if (!state.npc.IsEmpty())
        {
            npc = me->GetMap()->GetCreature(state.npc);
            if (!npc || !npc->IsAlive() || npc->IsInCombat() ||
                (!state.arrivedMs && WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS))
            {
                Finish(me, state, now);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            state.nextSearchMs = now + SEARCH_MS;
            npc = Pick(me, state, center, task, now);
            if (!npc)
                return false;
            state.npc = npc->GetObjectGuid();
            state.startedMs = now;
            state.nextMoveMs = 0;
            state.arrivedMs = 0;
        }

        // (the interaction check of the NPC handlers: distance, reputation, state)
        if (me->CanInteractWithNPC(npc, 0))
        {
            if (!state.arrivedMs)
            {
                if (!me->IsStopped())
                    me->StopMoving();
                me->SetFacingToObject(npc);
                me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                state.arrivedMs = now;
            }
            if (WorldTimer::getMSTimeDiff(state.arrivedMs, now) < DWELL_MS)
                return true;
            bool const did = task.Wants(me, npc);
            if (did)
                task.Do(me, npc);
            Finish(me, state, now);
            if (did)
                Errands::BeginBeat(state.after, anchor, now, AFTER_MIN_MS, AFTER_MAX_MS);
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
