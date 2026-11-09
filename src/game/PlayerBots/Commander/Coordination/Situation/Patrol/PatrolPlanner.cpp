#include "Commander/Coordination/Situation/Patrol/PatrolPlanner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
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

        // Moving there takes the hider towards an idle hostile creature.
        bool TowardsIdleCreature(WorldModel const& world, Position3 const& point, Position3 const& from)
        {
            for (PackFacts const& pack : world.packs)
            {
                for (PackCreature const& c : pack.members)
                {
                    if (!c.aggressive || c.noAggro)
                        continue;
                    float const d = PackResolver::Distance(c.pos, point);
                    if (d < PatrolPlanner::OTHER_CREATURE_CLEARANCE && d < PackResolver::Distance(c.pos, from))
                        return true;
                }
            }
            return false;
        }

        // Main tank and healer bots that must still see the hider at the point.
        std::vector<Unit const*> Watchers(WorldModel const& world, Map* map, ObjectGuid hider)
        {
            std::vector<Unit const*> out;
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            for (PartyMemberFacts const& m : world.party)
            {
                if (out.size() >= PatrolPlanner::MAX_WATCHERS)
                    break;
                if (m.isPet || !m.alive || m.guid == hider)
                    continue;
                if (!(tank && m.guid == tank->guid) && !(m.isBot && m.role == ROLE_HEALER))
                    continue;
                if (Unit* u = map->GetUnit(m.guid))
                    out.push_back(u);
            }
            return out;
        }
    }

    void PatrolPlanner::Step(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        PatrolMovePlan& m = plan.patrolMove;
        if (!m.active || m.phase != PatrolPhase::Guard)
            return;
        PatrolFacts const* patrol = PatrolAssigner::FindPatrol(world, m);
        if (!patrol)
            return;
        float const remaining = Value::FightRemainingSec(world);

        for (PatrolLure& l : m.lures)
        {
            if (l.state != PatrolLureState::Search)
                continue;
            Unit* hider = map->GetUnit(l.hider);
            std::vector<Creature const*> casters;
            for (ObjectGuid g : l.casters)
                if (Creature* c = map->GetCreature(g))
                    if (c->IsAlive())
                        casters.push_back(c);
            if (!hider || !hider->IsAlive() || casters.empty())
            {
                l.state = PatrolLureState::NoPoint;
                continue;
            }

            Position3 const from = PositionOf(hider);
            LosBreak::Search search;
            search.center = from;
            search.casters = casters;
            search.mover = hider;
            search.watchers = Watchers(world, map, l.hider);
            search.rejects = [&](Position3 const& p)
            {
                return PatrolAssigner::InPath(*patrol, p, remaining) || TowardsIdleCreature(world, p, from);
            };

            char buf[256];
            if (LosBreak::FindPoint(map, search, l.point))
            {
                l.state = PatrolLureState::Active;
                l.sinceMs = nowMs;
                snprintf(buf, sizeof(buf), "%s casts from the patrol's path: %s steps out of its sight (%.0f yd) so it comes out.",
                    casters.front()->GetName(), hider->GetName(), PackResolver::Distance(from, l.point));
            }
            else
            {
                l.state = PatrolLureState::NoPoint;
                snprintf(buf, sizeof(buf), "%s casts from the patrol's path; no spot out of its sight near %s: killing it from range.",
                    casters.front()->GetName(), hider->GetName());
            }
            plan.notes.push_back(buf);
        }
    }
}
