#include "Commander/Coordination/Situation/Crisis/RunAwayAssigner.h"
#include "Commander/Coordination/Situation/RetreatPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Tanks.h"
#include "Timer.h"
#include <cmath>
#include <cstdio>

namespace Commander
{
    namespace
    {
        // A leader who tanks (header): away from every other living member.
        bool LeftTheParty(WorldModel const& world, PartyMemberFacts const& leader)
        {
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive || m.guid == leader.guid)
                    continue;
                float const dx = m.pos.x - leader.pos.x, dy = m.pos.y - leader.pos.y;
                if (dx * dx + dy * dy < RunAwayAssigner::LEAVE_PARTY_YD * RunAwayAssigner::LEAVE_PARTY_YD)
                    return false;
            }
            return true;
        }
    }

    void RunAwayAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        RetreatPlan& r = plan.retreat;
        if (!world.HasPartyEnemy() || r.active || r.requested)
            return;
        if (r.declinedAtMs && WorldTimer::getMSTimeDiff(r.declinedAtMs, world.refreshedAtMs) < RETRY_MS)
            return;

        LeaderRunFacts const& run = world.leaderRun;
        char buf[160];
        if (run.leftInFight)
        {
            r.requested = true;
            r.requestWhy = "you left in the fight";
            return;
        }
        if (!run.running)
            return;

        std::string why;
        bool sustained = false;
        bool const danger = plan.crisis.lost != 0 || RetreatAssigner::InDanger(world, why, sustained);
        float const gain = danger ? DANGER_GAIN_YD : CALM_GAIN_YD;
        uint32 const ms = danger ? DANGER_RUN_MS : CALM_RUN_MS;
        if (run.gainYd < gain || run.runMs < ms)
            return;
        PartyMemberFacts const* leader = world.FindMember(world.leader);
        if (!leader || (Tanks::IsTank(*leader) && !LeftTheParty(world, *leader)))
            return;
        snprintf(buf, sizeof(buf), "you ran %.0f yd away from the fight in %.1f s%s", run.gainYd, run.runMs / 1000.0f,
            danger ? " (we are in trouble)" : "");
        r.requested = true;
        r.requestWhy = buf;
    }

    void RunAwayAssigner::Clear(TeamPlan& plan) const
    {
        plan.retreat.requested = false;
    }
}
