#include "Commander/Coordination/Situation/Patrol/PatrolPlanner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Creature.h"
#include "Map.h"
#include <cstdio>

namespace Commander
{
    namespace
    {
        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }
    }

    void PatrolPlanner::Step(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        PatrolGuardPlan& m = plan.patrolGuard;
        if (!m.active)
            return;
        PatrolFacts const* patrol = PatrolAssigner::FindPatrol(world, m);
        if (!patrol)
            return;
        float const remaining = Value::FightRemainingSec(world);

        for (LosLure& l : m.lures)
        {
            if (l.state != LosLureState::Search)
                continue;
            Unit* hider = map->GetUnit(l.hider);
            std::vector<Creature const*> casters;
            for (ObjectGuid g : l.casters)
                if (Creature* c = map->GetCreature(g))
                    if (c->IsAlive())
                        casters.push_back(c);
            if (!hider || !hider->IsAlive() || casters.empty())
            {
                l.state = LosLureState::NoPoint;
                continue;
            }

            Position3 const from = PositionOf(hider);
            LosBreak::Search search;
            search.center = from;
            search.casters = casters;
            search.mover = hider;
            search.watchers = LosBreak::Watchers(world, map, l.hider);
            search.rejects = [&](Position3 const& p)
            {
                return PatrolZone::Comes(world, *patrol, p, remaining) || LosBreak::TowardsIdleCreature(world, p, from);
            };

            char buf[256];
            if (LosBreak::FindPoint(map, search, l.point))
            {
                l.state = LosLureState::Active;
                l.sinceMs = nowMs;
                snprintf(buf, sizeof(buf), "%s casts from the patrol's path: %s steps out of its sight (%.0f yd) so it comes out.",
                    casters.front()->GetName(), hider->GetName(), PackResolver::Distance(from, l.point));
            }
            else
            {
                l.state = LosLureState::NoPoint;
                snprintf(buf, sizeof(buf), "%s casts from the patrol's path; no spot out of its sight near %s: killing it from range.",
                    casters.front()->GetName(), hider->GetName());
            }
            plan.notes.push_back(buf);
        }
    }
}
