#include "Commander/Perception/Values/SightMargin.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SightCache.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Commander
{
namespace SightMargin
{
    namespace
    {
        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }
    }

    std::vector<Position3> Points(WorldModel const& world, Position3 const& target, ObjectGuid targetGuid,
                                  ObjectGuid member)
    {
        // ("near" is a macro in the Windows headers)
        std::vector<std::pair<float, Position3>> around;
        auto const add = [&](Position3 const& at)
        {
            float const d = Distance2d(at, target);
            if (d > PositionField::CELL_YD)
                around.emplace_back(d, at);
        };
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.guid == targetGuid)
            {
                if (!e.victim.IsEmpty() && e.victim != member)
                    if (PartyMemberFacts const* v = world.FindMember(e.victim))
                        if (v->alive)
                            add(v->pos);
                continue;
            }
            if (!e.acting || e.onAllyOnly || Distance2d(e.pos, target) > PILE_YD)
                continue;
            add(e.pos);
        }
        std::sort(around.begin(), around.end(),
                  [](std::pair<float, Position3> const& a, std::pair<float, Position3> const& b) { return a.first < b.first; });
        std::vector<Position3> points;
        for (size_t i = 0; i < around.size() && i < MAX_POINTS; ++i)
            points.push_back(around[i].second);
        return points;
    }

    float LossAt(WorldModel const& world, PartyMemberFacts const& member, Unit const* viewer, SightCache& sight,
                 std::vector<Position3> const& points, Position3 const& p, uint32& budget)
    {
        if (points.empty())
            return 0.0f;
        uint32 hidden = 0;
        for (Position3 const& q : points)
            if (sight.Cells(viewer, p, q, budget) == SightCache::Sight::No)
                ++hidden;
        if (!hidden)
            return 0.0f;
        return MOVE_CHANCE * float(hidden) / float(points.size()) * Value::LostAction(world, member, REACQUIRE_SEC);
    }
}
}
