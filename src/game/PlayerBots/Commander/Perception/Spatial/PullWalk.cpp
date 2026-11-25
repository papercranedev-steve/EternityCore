#include "Commander/Perception/Spatial/PullWalk.h"
#include "Creature.h"
#include "PathFinder.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
namespace PullWalk
{
    namespace
    {
        Position3 ToPosition(Vector3 const& v)
        {
            Position3 p;
            p.x = v.x;
            p.y = v.y;
            p.z = v.z;
            return p;
        }
    }

    Creature const* FirstBlocker(SafeRoute::IdleCreatures const& idle, PathInfo const& path,
                                 std::vector<ObjectGuid> const& pack, std::vector<PatrolFacts> const& patrols)
    {
        std::vector<ObjectGuid> leftOut = pack;
        for (PatrolFacts const& p : patrols)
            leftOut.push_back(p.guid);
        SafeRoute::IdleCreatures others = idle;
        others.Ignore(leftOut);
        SafeRoute::IdleCreatures own = idle;
        own.KeepOnly(pack);

        auto const& pts = path.getPath();
        for (size_t i = 1; i < pts.size(); ++i)
        {
            Position3 const a = ToPosition(pts[i - 1]);
            Position3 const b = ToPosition(pts[i]);
            uint32 const steps = std::max(1u, uint32(SafeRoute::Distance2d(a, b) / SafeRoute::PATH_SAMPLE_STEP));
            for (uint32 s = 1; s <= steps; ++s)
            {
                float const f = float(s) / steps;
                Position3 p;
                p.x = a.x + (b.x - a.x) * f;
                p.y = a.y + (b.y - a.y) * f;
                p.z = a.z + (b.z - a.z) * f;
                if (own.Near(p))
                    return nullptr;
                if (Creature const* c = others.Blocker(p))
                    return c;
            }
        }
        return nullptr;
    }

    Creature const* Blocker(Player* walker, SafeRoute::IdleCreatures const& idle, Position3 const& to,
                            std::vector<ObjectGuid> const& pack, std::vector<PatrolFacts> const& patrols)
    {
        PathInfo path(walker);
        path.calculate(to.x, to.y, to.z);
        if ((path.getPathType() & (PATHFIND_NOT_USING_PATH | PATHFIND_NOPATH | PATHFIND_INCOMPLETE)) ||
            path.getPath().empty())
            return nullptr;
        return FirstBlocker(idle, path, pack, patrols);
    }

    std::vector<ObjectGuid> PackOf(WorldModel const& world, ObjectGuid creature)
    {
        std::vector<ObjectGuid> out{ creature };
        for (PackFacts const& pack : world.packs)
        {
            bool const holds = std::any_of(pack.members.begin(), pack.members.end(),
                [creature](PackCreature const& c) { return c.guid == creature; });
            if (!holds)
                continue;
            for (PackCreature const& c : pack.members)
                if (c.guid != creature)
                    out.push_back(c.guid);
            break;
        }
        return out;
    }
}
}
