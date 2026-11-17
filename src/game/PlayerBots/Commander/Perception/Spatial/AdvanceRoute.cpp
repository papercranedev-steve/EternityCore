#include "Commander/Perception/Spatial/AdvanceRoute.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Creature.h"
#include "PathFinder.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Commander
{
namespace AdvanceRoute
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

        struct Sample
        {
            Position3 pos;
            float yd;
        };
    }

    Leg Toward(Player* walker, Position3 const& goal, float maxYd)
    {
        Leg leg;
        Position3 start;
        start.x = walker->GetPositionX();
        start.y = walker->GetPositionY();
        start.z = walker->GetPositionZ();
        leg.end = start;

        PathInfo path(walker);
        path.calculate(goal.x, goal.y, goal.z);
        uint32 const type = path.getPathType();
        if ((type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)) || path.getPath().size() < 2)
            return leg;
        leg.path = true;

        std::vector<Position3> points;
        points.reserve(path.getPath().size());
        for (auto const& v : path.getPath())
        {
            Position3 p;
            p.x = v.x;
            p.y = v.y;
            p.z = v.z;
            points.push_back(p);
        }
        for (size_t i = 1; i < points.size(); ++i)
            leg.goalYd += Dist3(points[i - 1], points[i]);

        SafeRoute::IdleCreatures const idle = SafeRoute::IdleCreatures(walker, start,
            maxYd + IDLE_SCAN_MARGIN).LeavingFrom(start);

        // Samples every PATH_SAMPLE_STEP up to maxYd; the last one exactly at maxYd or the end.
        std::vector<Sample> safe;
        safe.push_back({ start, 0.0f });
        float walked = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
        {
            Position3 const& a = points[i - 1];
            Position3 const& b = points[i];
            float const segment = Dist3(a, b);
            if (segment < 0.01f)
                continue;
            uint32 const steps = std::max(1u, uint32(std::ceil(segment / SafeRoute::PATH_SAMPLE_STEP)));
            for (uint32 s = 1; s <= steps; ++s)
            {
                float const along = walked + segment * float(s) / float(steps);
                bool const last = along >= maxYd;
                float const yd = last ? maxYd : along;
                Position3 const p = Lerp(a, b, (yd - walked) / segment);
                if (Creature const* c = idle.Blocker(p))
                {
                    leg.blocker = c->GetObjectGuid();
                    // Back off along the walk: the last safe sample STOP_BACK_YD before this one.
                    Sample stop = safe.front();
                    for (Sample const& x : safe)
                        if (x.yd <= yd - STOP_BACK_YD)
                            stop = x;
                    leg.end = stop.pos;
                    leg.yd = stop.yd;
                    return leg;
                }
                safe.push_back({ p, yd });
                if (last)
                {
                    leg.end = p;
                    leg.yd = yd;
                    leg.reachesGoal = i + 1 == points.size() && s == steps;
                    return leg;
                }
            }
            walked += segment;
        }
        leg.end = safe.back().pos;
        leg.yd = safe.back().yd;
        leg.reachesGoal = true;
        return leg;
    }
}
}
