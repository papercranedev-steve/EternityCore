#include "Life/Errands/ErrandCommon.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Player.h"
#include "Group.h"
#include "MotionMaster.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Life
{
namespace Errands
{
    bool GroupFights(Player* me)
    {
        Group* group = me->GetGroup();
        if (!group)
            return me->IsInCombat();
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* member = itr->getSource())
                if (member->IsAlive() && member->IsInCombat())
                    return true;
        return false;
    }

    bool MayRun(Player* me, Player const* anchor, bool seatedOk)
    {
        return me->IsAlive() && !me->IsInCombat() && !me->IsMounted() &&
               (seatedOk || me->GetStandState() == UNIT_STAND_STATE_STAND) &&
               !(anchor && anchor->IsMounted()) && !GroupFights(me);
    }

    bool WalkIsSafe(Player* me, float x, float y, float z, float maxDetour)
    {
        Commander::Position3 to;
        to.x = x;
        to.y = y;
        to.z = z;
        Commander::Position3 centre;
        centre.x = (me->GetPositionX() + x) * 0.5f;
        centre.y = (me->GetPositionY() + y) * 0.5f;
        centre.z = (me->GetPositionZ() + z) * 0.5f;
        float const dx = me->GetPositionX() - x;
        float const dy = me->GetPositionY() - y;
        float const radius = std::sqrt(dx * dx + dy * dy) * 0.5f + Commander::SafeRoute::SCAN_RADIUS;
        Commander::SafeRoute::IdleCreatures const idle(me, centre, radius);
        return Commander::SafeRoute::PathIsSafe(me, idle, to, maxDetour);
    }

    bool IsDone(DoneList const& done, ObjectGuid guid, uint32 forgetAfterMs)
    {
        uint32 const now = WorldTimer::getMSTime();
        return std::any_of(done.begin(), done.end(), [&](DoneMark const& mark)
        {
            return mark.guid == guid && (!forgetAfterMs || WorldTimer::getMSTimeDiff(mark.atMs, now) < forgetAfterMs);
        });
    }

    void MarkDone(DoneList& done, ObjectGuid guid, size_t keep)
    {
        DoneMark mark;
        mark.guid = guid;
        mark.atMs = WorldTimer::getMSTime();
        done.push_back(mark);
        while (done.size() > keep)
            done.pop_front();
    }

    bool CloseEnoughToUse(Player const* me, WorldObject const* target, float nearYd, bool walked)
    {
        float constexpr SLACK_YD = 1.0f;
        float const dx = me->GetPositionX() - target->GetPositionX();
        float const dy = me->GetPositionY() - target->GetPositionY();
        float const closeYd = nearYd + SLACK_YD;
        if (dx * dx + dy * dy <= closeYd * closeYd)
            return true;
        return walked && me->IsStopped();
    }

    void StopWalk(Player* me)
    {
        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }
}
}
