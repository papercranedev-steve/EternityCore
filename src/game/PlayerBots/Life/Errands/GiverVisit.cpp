#include "Life/Errands/GiverVisit.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "MotionMaster.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Life
{
namespace GiverVisit
{
    namespace
    {
        // The bot's place among the party's bots (by guid, the leader left out): its spot in the arc.
        uint32 SlotOf(Player* me, Player const* leader)
        {
            Group* group = me->GetGroup();
            if (!group)
                return 0;
            std::vector<uint64> bots;
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player const* p = itr->getSource();
                if (p && p->IsBot() && p != leader)
                    bots.push_back(p->GetObjectGuid().GetRawValue());
            }
            std::sort(bots.begin(), bots.end());
            auto const it = std::find(bots.begin(), bots.end(), me->GetObjectGuid().GetRawValue());
            return it == bots.end() ? 0 : uint32(it - bots.begin());
        }

        // Beside the leader in front of the giver: +a, -a, +2a, -2a ... from the giver-leader line
        // (the leader's own place in the middle), a second row a little further out.
        void SpotFor(Player* me, WorldObject const* giver, Player const* leader, float& x, float& y, float& z)
        {
            constexpr uint32 PER_ROW = 8;
            constexpr float STEP_RAD = 0.6f;
            uint32 const slot = SlotOf(me, leader);
            uint32 const inRow = slot % PER_ROW;
            float const side = (inRow % 2) ? -1.0f : 1.0f;
            float const offset = side * STEP_RAD * float(inRow / 2 + 1);
            float const radius = std::min(SPOT_YD + 1.2f * float(slot / PER_ROW), INTERACTION_DISTANCE - 0.5f);
            WorldObject const* facing = leader && leader->IsInMap(giver) && giver->IsWithinDist(leader, TOGETHER_YD, false)
                ? static_cast<WorldObject const*>(leader) : me;
            float const angle = giver->GetAngle(facing) + offset;
            x = giver->GetPositionX();
            y = giver->GetPositionY();
            z = giver->GetPositionZ();
            giver->GetNearPoint(me, x, y, z, me->GetObjectBoundingRadius(), radius, angle);
        }

        void Start(GiverVisitState& state, ObjectGuid giver, uint32 nowMs)
        {
            state.giver = giver;
            state.spotSet = false;
            state.direct = false;
            state.walkSinceMs = nowMs;
            state.nextMoveMs = 0;
            state.arrivedMs = 0;
        }
    }

    bool InReach(Player const* me, WorldObject const* giver)
    {
        if (GameObject const* go = giver->ToGameObject())
            return go->IsAtInteractDistance(me);
        return me->IsWithinDistInMap(giver, INTERACTION_DISTANCE);
    }

    bool LeaderBy(Player const* leader, WorldObject const* giver)
    {
        return leader && leader->IsInMap(giver) && giver->IsWithinDist(leader, TOGETHER_YD, false);
    }

    Step Approach(Player* me, GiverVisitState& state, WorldObject* giver, Player const* leader, uint32 nowMs)
    {
        if (state.giver != giver->GetObjectGuid())
            Start(state, giver->GetObjectGuid(), nowMs);

        if (state.arrivedMs)
        {
            bool const again = state.lastGiver == state.giver && state.lastActMs &&
                               WorldTimer::getMSTimeDiff(state.lastActMs, nowMs) < AGAIN_WINDOW_MS;
            uint32 const hold = again ? AGAIN_HOLD_MS : HOLD_MS;
            return WorldTimer::getMSTimeDiff(state.arrivedMs, nowMs) >= hold ? Step::Act : Step::Holding;
        }

        if (!state.spotSet)
        {
            SpotFor(me, giver, leader, state.spotX, state.spotY, state.spotZ);
            state.spotSet = true;
        }

        bool const reach = InReach(me, giver);
        bool const atSpot = me->GetDistance2d(state.spotX, state.spotY) <= SPOT_REACHED_YD;
        if (reach && (atSpot || state.direct || WorldTimer::getMSTimeDiff(state.walkSinceMs, nowMs) > SPOT_GIVE_UP_MS))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->SetFacingToObject(giver);
            bool const again = state.lastGiver == state.giver && state.lastActMs &&
                               WorldTimer::getMSTimeDiff(state.lastActMs, nowMs) < AGAIN_WINDOW_MS;
            if (!again)
                me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
            state.arrivedMs = nowMs;
            return Step::Holding;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return Step::Walking;

        // On the spot but out of reach (a counter, a big object): to the giver itself.
        if (atSpot && !reach && !state.direct)
        {
            state.direct = true;
            state.nextMoveMs = 0;
        }
        if (nowMs >= state.nextMoveMs)
        {
            state.nextMoveMs = nowMs + REPATH_MS;
            if (state.direct)
                me->GetMotionMaster()->MovePoint(0, giver->GetPositionX(), giver->GetPositionY(), giver->GetPositionZ(),
                                                 MOVE_PATHFINDING | MOVE_RUN_MODE);
            else
                me->GetMotionMaster()->MovePoint(0, state.spotX, state.spotY, state.spotZ, MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return Step::Walking;
    }

    void Acted(GiverVisitState& state, uint32 nowMs)
    {
        state.lastGiver = state.giver;
        state.lastActMs = nowMs;
        state.lingerUntilMs = nowMs + LINGER_MS;
        state.arrivedMs = 0;
        state.spotSet = false;
        state.giver.Clear();
    }

    bool Lingering(Player* me, GiverVisitState& state, uint32 nowMs)
    {
        if (!state.lingerUntilMs)
            return false;
        if (nowMs >= state.lingerUntilMs)
        {
            state.lingerUntilMs = 0;
            return false;
        }
        if (!me->IsStopped())
            me->StopMoving();
        return true;
    }

    void End(GiverVisitState& state)
    {
        state.giver.Clear();
        state.spotSet = false;
        state.direct = false;
        state.arrivedMs = 0;
        state.lingerUntilMs = 0;
    }
}
}
