#include "Commander/Coordination/Situation/Patrol/PatrolHold.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    void PatrolHoldAssigner::Clear(TeamPlan& plan) const
    {
        plan.patrolHold = PatrolHoldPlan();
    }

    void PatrolHoldAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        PatrolHoldPlan& h = plan.patrolHold;
        h = PatrolHoldPlan();
        DoctrineHold const* hold = nullptr;
        for (DoctrineHold const& x : plan.doctrine.holds)
            if (x.planned)
                hold = &x;
        if (!hold)
            return;
        PatrolFacts const* patrol = nullptr;
        for (PatrolFacts const& p : world.patrols)
            if (p.guid == hold->enemy)
                patrol = &p;
        PartyMemberFacts const* bot = world.FindMember(hold->bot);
        ControlAbility const* ability = nullptr;
        if (bot)
            for (ControlAbility const& a : bot->controls)
                if (a.spellId == hold->spellId)
                    ability = &a;
        if (!patrol || !ability)
            return;

        if (world.HasPartyEnemy())
        {
            PartyMemberFacts const* tank = Tanks::Main(world);
            if (!tank)
                return;
            h.fightAt = tank->pos;
        }
        else if (!plan.pull.PlannedFightSpot(h.fightAt))
            return;

        Trace::Scope scope("patrolhold");
        h.active = true;
        h.patrol = patrol->guid;
        h.bot = bot->guid;
        h.spellId = ability->spellId;
        h.clearYd = ControlValue::ClearOfPileYd(world);
        PatrolZone::Approach const approach = PatrolZone::FirstWithin(world, *patrol, h.fightAt, h.clearYd);
        if (approach.sec < 0.0f || approach.index == 0)
        {
            // Not on its track within reach of the pile (it comes some other way), or already
            // inside: held where it is when it walks into range.
            h.holdAt = patrol->pos;
            h.holdSec = 0.0f;
        }
        else
        {
            h.holdAt = patrol->track[approach.index - 1];
            h.holdSec = PatrolZone::SecOf(*patrol, approach.index - 1, PatrolZone::TrackAgeSec(world));
        }

        if (world.field)
        {
            FieldSpots::Query q;
            q.center = h.holdAt;
            q.minYd = 0.0f;
            q.maxYd = std::max(1.0f, ability->maxRange - RANGE_SLACK_YD);
            std::vector<FieldSpots::Spot> spots = FieldSpots::Collect(world, *bot, q);
            spots.erase(std::remove_if(spots.begin(), spots.end(), [&](FieldSpots::Spot const& s)
            {
                float const t = PatrolZone::ArriveSec(world, *patrol, s.pos);
                return t >= 0.0f && t < h.holdSec + ARRIVE_SLACK_SEC;
            }), spots.end());
            std::stable_sort(spots.begin(), spots.end(), [](FieldSpots::Spot const& a, FieldSpots::Spot const& b)
            {
                return a.WalkEstimate(PATH_FACTOR) < b.WalkEstimate(PATH_FACTOR);
            });
            for (size_t i = 0; i < spots.size() && i < MAX_SPOTS; ++i)
                h.spots.push_back(spots[i].pos);
            if (!spots.empty())
            {
                float const walkSec = spots.front().WalkEstimate(PATH_FACTOR) / RUN_YARDS_PER_SEC;
                float const leaveSec = h.holdSec - walkSec - LEAD_SEC;
                h.goAtMs = leaveSec > 0.0f ? world.refreshedAtMs + uint32(leaveSec * 1000.0f) : 0;
            }
        }
        char buf[200];
        snprintf(buf, sizeof(buf), "held about %.0f s from now, %.0f yd from the pile; %u spot(s), goes in %.0f s",
            h.holdSec, PackResolver::Distance(h.holdAt, h.fightAt), uint32(h.spots.size()),
            h.goAtMs ? (h.goAtMs - world.refreshedAtMs) / 1000.0f : 0.0f);
        h.why = buf;
        if (scope.On())
        {
            scope.Label("%s by %s", Trace::Guid(h.patrol).c_str(), Trace::Guid(h.bot).c_str());
            scope.Data(Trace::Format("{\"why\":\"%s\",\"clearYd\":%.1f,\"holdAt\":[%.1f,%.1f,%.1f]}", h.why.c_str(),
                h.clearYd, h.holdAt.x, h.holdAt.y, h.holdAt.z));
        }
    }
}
