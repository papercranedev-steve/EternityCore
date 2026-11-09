#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace RouteRisk
{
    namespace
    {
        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x;
            float const dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // 2D distance from `p` to the segment a-b.
        float SegmentDistance(Position3 const& a, Position3 const& b, Position3 const& p)
        {
            float const dx = b.x - a.x;
            float const dy = b.y - a.y;
            float const len2 = dx * dx + dy * dy;
            float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            Position3 q;
            q.x = a.x + t * dx;
            q.y = a.y + t * dy;
            return Distance2d(q, p);
        }

        bool Crosses(PackCreature const& c, Position3 const& from, Position3 const& to)
        {
            if (!c.aggressive || c.noAggro || c.tappedByOther || c.reach <= 0.0f)
                return false;
            float const reach = c.reach + MARGIN;
            if (Distance2d(from, c.pos) <= reach)
                return false;
            return SegmentDistance(from, to, c.pos) <= reach;
        }
    }

    bool CrossesIdle(WorldModel const& world, Position3 const& from, Position3 const& to)
    {
        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
                if (Crosses(c, from, to))
                    return true;
        for (PatrolFacts const& p : world.patrols)
            if (Crosses(p, from, to))
                return true;
        return false;
    }

    bool Blocked(WorldModel const& world, PartyMemberFacts const& bot, EnemyFacts const& enemy)
    {
        if (RouteFact const* r = world.FindRoute(bot.guid, enemy.guid))
            return !r->safe;
        return CrossesIdle(world, bot.pos, enemy.pos);
    }

    float WalkYd(WorldModel const& world, PartyMemberFacts const& bot, EnemyFacts const& enemy)
    {
        float const straight = PackResolver::Distance(bot.pos, enemy.pos);
        if (RouteFact const* r = world.FindRoute(bot.guid, enemy.guid))
            return std::max(straight, r->pathYd);
        return straight;
    }
}
}
