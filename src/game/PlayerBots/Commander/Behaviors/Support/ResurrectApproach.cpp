#include "Commander/Behaviors/Support/ResurrectApproach.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Corpse.h"
#include "Group.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <vector>

namespace Commander
{
namespace ResurrectApproach
{
    namespace
    {
        // What the resurrection is cast at: the body of a member that did not release (the
        // player itself), else the corpse on this map of a ghost. Null: nothing to resurrect
        // here (alive, a pending request, the body elsewhere, bones).
        WorldObject* BodyOf(Player* me, Player* member)
        {
            if (!member || member == me || member->IsRessurectRequested())
                return nullptr;

            if (member->GetDeathState() == CORPSE)
                return me->IsInMap(member) ? member : nullptr;

            if (member->GetDeathState() == DEAD)
            {
                // (as PartyBotAI::SelectReleasedCorpse: the ghost may be on another map)
                Corpse* corpse = sObjectAccessor.GetCorpseForPlayerGUID(member->GetObjectGuid());
                if (!corpse || corpse->GetType() == CORPSE_BONES ||
                    me->GetMap()->GetCorpse(corpse->GetObjectGuid()) != corpse)
                    return nullptr;
                return corpse;
            }
            return nullptr;
        }

        // The resurrection can be cast from here (as SelectResurrectionTarget / SelectReleasedCorpse).
        bool CanCastAt(Player* me, WorldObject* body, SpellEntry const* resurrection)
        {
            return resurrection->IsTargetInRange(me, body) && me->IsWithinLOSInMap(body);
        }

        bool PartyFights(Player* me)
        {
            for (GroupReference* itr = me->GetGroup()->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->getSource())
                    if (member != me && member->IsAlive() && member->IsInCombat())
                        return true;
            return false;
        }

        Position3 At(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        // No idle pack on the walk to `body` (the scan covers the whole walk).
        bool WalkIsSafe(Player* me, WorldObject const* body)
        {
            Position3 const to = At(body);
            Position3 centre;
            centre.x = (me->GetPositionX() + to.x) * 0.5f;
            centre.y = (me->GetPositionY() + to.y) * 0.5f;
            centre.z = (me->GetPositionZ() + to.z) * 0.5f;
            float const radius = me->GetDistance(body) * 0.5f + SafeRoute::SCAN_RADIUS;
            SafeRoute::IdleCreatures const idle(me, centre, radius);
            return SafeRoute::PathIsSafe(me, idle, to, MAX_PATH_DETOUR);
        }

        struct Candidate
        {
            Player* member;
            WorldObject* body;
            float dist;
        };

        // The nearest dead member to walk to. `unsafe`: one was left out for its walk.
        Player* Pick(Player* me, ResurrectApproachState const& state, SpellEntry const* resurrection,
                     uint32 now, bool& unsafe)
        {
            unsafe = false;
            std::vector<Candidate> candidates;
            for (GroupReference* itr = me->GetGroup()->GetFirstMember(); itr; itr = itr->next())
            {
                Player* member = itr->getSource();
                WorldObject* body = BodyOf(me, member);
                if (!body)
                    continue;
                if (member->GetObjectGuid() == state.left && now < state.leftUntilMs)
                    continue;
                float const dist = me->GetDistance(body);
                if (dist > SEARCH_YD || CanCastAt(me, body, resurrection))
                    continue;
                candidates.push_back({ member, body, dist });
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](Candidate const& a, Candidate const& b) { return a.dist < b.dist; });

            for (Candidate const& c : candidates)
            {
                if (WalkIsSafe(me, c.body))
                    return c.member;
                unsafe = true;
            }
            return nullptr;
        }

        void Leave(ResurrectApproachState& state, ObjectGuid member, uint32 untilMs)
        {
            state.left = member;
            state.leftUntilMs = untilMs;
        }
    }

    void Stop(Player* me, ResurrectApproachState& state)
    {
        if (state.target.IsEmpty())
            return;
        state.target.Clear();
        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }

    bool Update(PartyBotAI* bot, ResurrectApproachState& state, SpellEntry const* resurrection)
    {
        Player* me = bot->me;
        if (!me->GetGroup() || PartyFights(me))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        WorldObject* body = nullptr;
        if (!state.target.IsEmpty())
        {
            body = BodyOf(me, sObjectAccessor.FindPlayer(state.target));
            // Alive, a pending request, or in range and sight: UpdateAI's resurrection takes it.
            if (!body || CanCastAt(me, body, resurrection))
            {
                Stop(me, state);
                return false;
            }

            float const dist = me->GetDistance(body);
            if (dist < state.bestDist - PROGRESS_YD)
            {
                state.bestDist = dist;
                state.bestMs = now;
            }
            else if (WorldTimer::getMSTimeDiff(state.bestMs, now) > STUCK_MS)
            {
                Leave(state, state.target, now + GIVE_UP_MS);
                Stop(me, state);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;

            bool unsafe = false;
            Player* member = Pick(me, state, resurrection, now, unsafe);
            if (!member)
            {
                state.nextSearchMs = now + (unsafe ? UNSAFE_RETRY_MS : SEARCH_MS);
                return false;
            }
            body = BodyOf(me, member);
            state.target = member->GetObjectGuid();
            state.bestDist = me->GetDistance(body);
            state.bestMs = now;
            state.nextMoveMs = 0;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            // A pack may have wandered onto the way since the last walk order.
            if (state.nextMoveMs != 0 && !WalkIsSafe(me, body))
            {
                Leave(state, state.target, now + UNSAFE_RETRY_MS);
                Stop(me, state);
                return false;
            }
            if (me->GetStandState() != UNIT_STAND_STATE_STAND)
                me->SetStandState(UNIT_STAND_STATE_STAND);
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, body->GetPositionX(), body->GetPositionY(), body->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
