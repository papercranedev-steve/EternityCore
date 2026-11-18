#include "Shared/WalkLength.h"
#include "Player.h"
#include "PathFinder.h"
#include <cmath>

namespace BotShared
{
namespace WalkLength
{
    namespace
    {
        // The path's end this close to the target counts as reaching it.
        float const REACH_YD = 3.0f;

        float Distance(Point const& a, Point const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    float Between(Player const* walker, Point const& from, Point const& to)
    {
        PathInfo path(walker);
        path.calculate(Vector3(from.x, from.y, from.z), Vector3(to.x, to.y, to.z));
        uint32 const type = path.getPathType();
        if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
            return -1.0f;
        if ((type & PATHFIND_NOT_USING_PATH) || path.getPath().empty())
            return Distance(from, to);
        Vector3 const& end = path.getPath().back();
        Point last;
        last.x = end.x;
        last.y = end.y;
        last.z = end.z;
        if (Distance(last, to) > REACH_YD)
            return -1.0f;
        return path.Length();
    }
}
}
