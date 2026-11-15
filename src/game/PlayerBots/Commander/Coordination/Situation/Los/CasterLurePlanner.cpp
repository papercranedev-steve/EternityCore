#include "Commander/Coordination/Situation/Los/CasterLurePlanner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/Situation/Los/LosHealerSpot.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
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

    void CasterLurePlanner::Step(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        float const remaining = Value::FightRemainingSec(world);
        PartyMemberFacts const* tank = Tanks::Main(world, true);
        for (LosLure& l : plan.casterLure.lures)
        {
            if (l.state != LosLureState::Search)
                continue;
            Unit* hider = map->GetUnit(l.hider);
            std::vector<Creature*> casters;
            for (ObjectGuid g : l.casters)
                if (Creature* c = map->GetCreature(g))
                    if (c->IsAlive())
                        casters.push_back(c);
            PartyMemberFacts const* facts = world.FindMember(l.hider);
            if (!hider || !hider->IsAlive() || !facts || casters.empty())
            {
                l.state = LosLureState::NoPoint;
                continue;
            }

            Position3 const from = PositionOf(hider);
            auto const rejects = [&](Position3 const& p)
            {
                return PatrolZone::Comes(world, p, remaining) || LosBreak::TowardsIdleCreature(world, p, from);
            };

            bool found = false;
            bool behindTank = false;
            if (facts->role == ROLE_HEALER && tank && tank->guid != l.hider && tank->alive)
                if (Player* healer = map->GetPlayer(l.hider))
                    found = behindTank = LosHealerSpot::Find(map, healer, tank->pos, casters, rejects, l.point);
            if (!found)
            {
                LosBreak::Search search;
                search.center = from;
                search.casters.assign(casters.begin(), casters.end());
                search.mover = hider;
                search.watchers = LosBreak::Watchers(world, map, l.hider);
                search.rejects = rejects;
                found = LosBreak::FindPoint(map, search, l.point);
            }

            char buf[256];
            if (found)
            {
                l.state = LosLureState::Active;
                l.sinceMs = nowMs;
                snprintf(buf, sizeof(buf), "%s stands back and casts at %s: %s steps out of its sight (%.0f yd%s) so it comes over.",
                    casters.front()->GetName(), hider->GetName(), hider->GetName(), PackResolver::Distance(from, l.point),
                    behindTank ? ", behind the tank" : "");
            }
            else
            {
                l.state = LosLureState::NoPoint;
                snprintf(buf, sizeof(buf), "%s stands back and casts at %s; no spot out of its sight nearby: fighting it from range.",
                    casters.front()->GetName(), hider->GetName());
            }
            plan.notes.push_back(buf);
        }
    }
}
