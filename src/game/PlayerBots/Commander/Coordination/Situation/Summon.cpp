#include "Commander/Coordination/Situation/Summon.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace Summon
{
    bool Start(TeamPlan& plan, Player const* warlock, ObjectGuid target, std::vector<ObjectGuid> const& helpers)
    {
        if (plan.summon.active || !warlock)
            return false;
        plan.summon = SummonPlan();
        plan.summon.active = true;
        plan.summon.warlock = warlock->GetObjectGuid();
        plan.summon.target = target;
        plan.summon.helpers = helpers;
        plan.summon.sinceMs = WorldTimer::getMSTime();
        return true;
    }

    void End(TeamPlan& plan, bool arrived)
    {
        plan.summon = SummonPlan();
        // He is back with the team: nobody waits for him any more.
        if (arrived)
            plan.stay.members.clear();
    }

    SummonRole RoleOf(TeamPlan const& plan, ObjectGuid member)
    {
        if (!plan.summon.active)
            return SummonRole::None;
        if (plan.summon.warlock == member)
            return SummonRole::Caster;
        if (std::find(plan.summon.helpers.begin(), plan.summon.helpers.end(), member) != plan.summon.helpers.end())
            return SummonRole::Helper;
        return SummonRole::None;
    }
}
}
