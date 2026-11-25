#include "Commander/Perception/Spatial/BringBackLine.h"
#include "Commander/Perception/Spatial/AggroRange.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Creature.h"
#include "PathFinder.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace BringBackLine
{
    namespace
    {
        float Dist3(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        Position3 Lerp(Position3 const& a, Position3 const& b, float f)
        {
            Position3 p;
            p.x = a.x + (b.x - a.x) * f;
            p.y = a.y + (b.y - a.y) * f;
            p.z = a.z + (b.z - a.z) * f;
            return p;
        }
    }

    Line Along(Player* walker, WorldModel const& world, Position3 const& from, Position3 const& to,
               std::vector<ObjectGuid> const& pulled)
    {
        Line line;
        std::vector<Position3> corners;
        PathInfo path(walker);
        path.calculate(Vector3(from.x, from.y, from.z), Vector3(to.x, to.y, to.z));
        uint32 const type = path.getPathType();
        if (type & PATHFIND_NOT_USING_PATH)
            corners = { from, to };
        else if ((type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)) || path.getPath().size() < 2)
            return line;
        else
        {
            for (auto const& v : path.getPath())
            {
                Position3 p;
                p.x = v.x;
                p.y = v.y;
                p.z = v.z;
                corners.push_back(p);
            }
        }
        line.path = true;
        for (size_t i = 1; i < corners.size(); ++i)
            line.yd += Dist3(corners[i - 1], corners[i]);

        Position3 const middle = Lerp(from, to, 0.5f);
        float const scan = Dist3(from, to) * 0.5f + AggroRange::MAX_ATTACK_DISTANCE;
        SafeRoute::IdleCreatures idle(walker, middle, scan, SafeRoute::IDLE_MARGIN);
        idle.Ignore(pulled);

        auto add = [&](Position3 const& p, float yd)
        {
            Point point;
            point.pos = p;
            point.yd = yd;
            point.risk = PullOdds::NearbyAdds(world, p, pulled);
            if (Creature const* c = idle.Blocker(p))
                point.aggroBy = c->GetObjectGuid();
            line.points.push_back(std::move(point));
        };

        add(corners.front(), 0.0f);
        float walked = 0.0f;
        for (size_t i = 1; i < corners.size(); ++i)
        {
            Position3 const& a = corners[i - 1];
            Position3 const& b = corners[i];
            float const segment = Dist3(a, b);
            if (segment < 0.01f)
                continue;
            uint32 const steps = std::max(1u, uint32(std::ceil(segment / SAMPLE_STEP)));
            for (uint32 s = 1; s <= steps; ++s)
            {
                float const f = float(s) / float(steps);
                add(Lerp(a, b, f), walked + segment * f);
            }
            walked += segment;
        }
        return line;
    }
}
}
