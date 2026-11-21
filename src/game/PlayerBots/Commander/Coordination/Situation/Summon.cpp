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
        SummonPlan& s = plan.summon;
        if (!warlock || s.RitualOf(warlock->GetObjectGuid()) || s.RitualOf(target))
            return false;
        for (ObjectGuid h : helpers)
            if (s.RitualOf(h))
                return false;
        SummonRitual r;
        r.warlock = warlock->GetObjectGuid();
        r.target = target;
        r.helpers = helpers;
        r.sinceMs = WorldTimer::getMSTime();
        s.rituals.push_back(r);
        return true;
    }

    void End(TeamPlan& plan, ObjectGuid warlock, bool leaderBack)
    {
        std::vector<SummonRitual>& rituals = plan.summon.rituals;
        rituals.erase(std::remove_if(rituals.begin(), rituals.end(),
                                     [warlock](SummonRitual const& r) { return r.warlock == warlock; }),
                      rituals.end());
        // He is back with the team: nobody waits for him any more.
        if (leaderBack)
            plan.stay.members.clear();
    }

    void MarkSent(TeamPlan& plan, ObjectGuid target, uint32 nowMs)
    {
        auto& sent = plan.summon.sent;
        sent.erase(std::remove_if(sent.begin(), sent.end(), [&](std::pair<ObjectGuid, uint32> const& p)
        {
            return p.first == target || WorldTimer::getMSTimeDiff(p.second, nowMs) > SENT_HOLD_MS;
        }), sent.end());
        sent.emplace_back(target, nowMs);
    }

    bool RecentlySent(TeamPlan const& plan, ObjectGuid target, uint32 nowMs)
    {
        for (auto const& p : plan.summon.sent)
            if (p.first == target && WorldTimer::getMSTimeDiff(p.second, nowMs) <= SENT_HOLD_MS)
                return true;
        return false;
    }

    SummonRole RoleOf(TeamPlan const& plan, ObjectGuid member)
    {
        for (SummonRitual const& r : plan.summon.rituals)
        {
            if (r.warlock == member)
                return SummonRole::Caster;
            if (std::find(r.helpers.begin(), r.helpers.end(), member) != r.helpers.end())
                return SummonRole::Helper;
        }
        return SummonRole::None;
    }

    bool CanCast(Player const* warlock, uint32 keep)
    {
        return warlock && warlock->HasSpell(RITUAL_OF_SUMMONING) && warlock->GetItemCount(SOUL_SHARD) > keep;
    }
}
}
