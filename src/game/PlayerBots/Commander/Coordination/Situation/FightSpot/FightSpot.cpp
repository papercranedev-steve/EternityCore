#include "Commander/Coordination/Situation/FightSpot/FightSpot.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace FightSpot
{
    Request InFight(WorldModel const& world, PartyMemberFacts const& tank, float patrolWithinSec)
    {
        Request r;
        r.at = tank.pos;
        r.remainingSec = Value::FightRemainingSec(world);
        r.patrolWithinSec = patrolWithinSec;
        return r;
    }

    Choice Choose(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank, Request const& r)
    {
        Choice c;
        std::vector<SpotCost::HeldMez> const held = SpotCost::HeldMezzes(world);
        float const clearYd = ControlValue::ClearOfPileYd(world);
        c.herePatrols = SpotCost::PatrolJoins(world, tank, r.at, r.remainingSec, r.patrolWithinSec, &c.patrol);
        c.hereHolds = SpotCost::BreaksHold(held, r.at, clearYd);
        c.hereLoss = c.herePatrols + c.hereHolds;
        float const margin = Value::LostAction(world, tank, MARGIN_SEC);
        if (c.hereLoss <= margin || !world.field)
            return c;

        Trace::Scope scope("fightspot");
        FieldSpots::Query q;
        q.center = r.at;
        q.minYd = MIN_YD;
        q.maxYd = MAX_YD;
        struct Scored
        {
            Position3 pos;
            float loss = 0.0f;
            float walk = 0.0f;
        };
        std::vector<Scored> scored;
        float const areaEnd = plan.area.active ? plan.area.endCost : 0.0f;
        for (FieldSpots::Spot const& s : FieldSpots::Collect(world, tank, q))
        {
            Scored x;
            x.pos = s.pos;
            x.loss = SpotCost::PatrolJoins(world, tank, s.pos, r.remainingSec, r.patrolWithinSec) +
                     SpotCost::BreaksHold(held, s.pos, clearYd);
            float const walkYd = r.walkFromAt ? PackResolver::Distance(r.at, s.pos) * PATH_FACTOR : s.WalkEstimate(PATH_FACTOR);
            x.walk = Value::FightExtension(world, walkYd / WALK_YARDS_PER_SEC) + areaEnd;
            if (x.loss + x.walk < c.hereLoss - margin)
                scored.push_back(x);
        }
        std::stable_sort(scored.begin(), scored.end(), [](Scored const& a, Scored const& b)
        {
            return a.loss + a.walk < b.loss + b.walk;
        });
        for (size_t i = 0; i < scored.size() && i < MAX_SPOTS; ++i)
            c.spots.push_back(scored[i].pos);
        if (!scored.empty())
        {
            c.bestLoss = scored.front().loss;
            c.bestWalk = scored.front().walk;
        }
        char buf[200];
        snprintf(buf, sizeof(buf), "here %.1f (patrols %.1f, holds %.1f); best %s %.1f + walk %.1f",
            c.hereLoss, c.herePatrols, c.hereHolds, scored.empty() ? "none" : "spot", c.bestLoss, c.bestWalk);
        c.why = buf;
        if (scope.On())
        {
            scope.Label("%s", c.why.c_str());
            scope.Data(Trace::Format("{\"candidates\":%u,\"held\":%u,\"clearYd\":%.1f}", uint32(scored.size()),
                uint32(held.size()), clearYd));
        }
        return c;
    }
}
}
