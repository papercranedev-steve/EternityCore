#include "Commander/Coordination/Situation/Los/LosHealerSpot.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Creature.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace LosHealerSpot
    {
        namespace
        {
            // Distance in the ground plane from `p` to the segment a-b.
            float SegmentDistance2d(Position3 const& p, Position3 const& a, Position3 const& b)
            {
                float const dx = b.x - a.x, dy = b.y - a.y;
                float const len2 = dx * dx + dy * dy;
                float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
                t = std::max(0.0f, std::min(1.0f, t));
                float const cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
                return std::sqrt(cx * cx + cy * cy);
            }

            Position3 PositionOf(WorldObject const* obj)
            {
                Position3 p;
                p.x = obj->GetPositionX();
                p.y = obj->GetPositionY();
                p.z = obj->GetPositionZ();
                return p;
            }

            // The caster's walk to `to` passes within PATH_PASS of `tankPoint`.
            bool PassesTank(Creature const* caster, Position3 const& to, Position3 const& tankPoint)
            {
                std::vector<Position3> line;
                PathInfo path(caster);
                path.calculate(to.x, to.y, to.z);
                uint32 const type = path.getPathType();
                if (!(type & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)))
                    for (auto const& v : path.getPath())
                    {
                        Position3 p;
                        p.x = v.x;
                        p.y = v.y;
                        p.z = v.z;
                        line.push_back(p);
                    }
                if (line.size() < 2)
                {
                    line.clear();
                    line.push_back(PositionOf(caster));
                    line.push_back(to);
                }
                for (size_t i = 1; i < line.size(); ++i)
                    if (SegmentDistance2d(tankPoint, line[i - 1], line[i]) <= PATH_PASS)
                        return true;
                return false;
            }
        }

        bool Find(Map* map, Player* healer, Position3 const& tankPoint, std::vector<Creature*> const& casters,
                  std::function<bool(Position3 const&)> const& rejects, Position3& point)
        {
            if (!healer || casters.empty())
                return false;
            Position3 const firstAt = PositionOf(casters.front());
            float const tankFromCaster = PackResolver::Distance(firstAt, tankPoint);

            LosBreak::Search search;
            search.center = tankPoint;
            search.casters.assign(casters.begin(), casters.end());
            search.mover = healer;
            search.sees.push_back(tankPoint);
            search.rejects = [&](Position3 const& p)
            {
                float const fromTank = PackResolver::Distance(p, tankPoint);
                if (fromTank > HEAL_RANGE || fromTank < MIN_FROM_TANK)
                    return true;
                if (PackResolver::Distance(firstAt, p) < tankFromCaster + MIN_BEHIND)
                    return true;
                return rejects && rejects(p);
            };
            search.accepts = [&](Position3 const& p)
            {
                return std::all_of(casters.begin(), casters.end(),
                    [&](Creature const* c) { return PassesTank(c, p, tankPoint); });
            };
            return LosBreak::FindPoint(map, search, point);
        }
    }
}
