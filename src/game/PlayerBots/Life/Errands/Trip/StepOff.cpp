#include "Life/Errands/Trip/StepOff.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include <cmath>

namespace Life
{
namespace Trip
{
namespace StepOff
{
    namespace
    {
        // A path whose end is this close (2d) to where it was asked to go got there.
        float const END_YD = 1.0f;
        float const END_HEIGHT_YD = 3.0f;
        // Farther than this, a zero-length path means the bot cannot leave.
        float const TRAP_MIN_YD = 2.0f;
        float const ZERO_PATH_YD = 0.3f;
        // Exit spots: rings around the bot, nearest first.
        float const RINGS[] = { 1.0f, 1.75f, 2.5f, 3.5f, EXIT_MAX_YD };
        uint32 const ANGLES = 12;
        float const MAX_STEP_HEIGHT_YD = 2.0f;

        float Distance2d(Spot const& a, Spot const& b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        Spot Here(Player* me)
        {
            Spot s;
            s.x = me->GetPositionX();
            s.y = me->GetPositionY();
            s.z = me->GetPositionZ();
            return s;
        }

        // The bot's path from `from` to `to` gets there. *length: its yards.
        bool Reaches(Player* me, Spot const& from, Spot const& to, float* length = nullptr)
        {
            PathInfo path(me);
            path.calculate(Vector3(from.x, from.y, from.z), Vector3(to.x, to.y, to.z));
            uint32 const type = path.getPathType();
            if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
                return false;
            if ((type & PATHFIND_NOT_USING_PATH) || path.getPath().empty())
            {
                if (length)
                    *length = Distance2d(from, to);
                return true;
            }
            Vector3 const& end = path.getPath().back();
            Spot last;
            last.x = end.x;
            last.y = end.y;
            last.z = end.z;
            if (length)
                *length = path.Length();
            return Distance2d(last, to) <= END_YD && std::fabs(last.z - to.z) <= END_HEIGHT_YD;
        }
    }

    bool Leavable(Player* me, Spot const& spot, Spot const& back)
    {
        return Reaches(me, back, spot) && Reaches(me, spot, back);
    }

    bool Trapped(Player* me, Spot const& to)
    {
        Spot const here = Here(me);
        if (Distance2d(here, to) <= TRAP_MIN_YD)
            return false;
        PathInfo path(me);
        path.calculate(Vector3(here.x, here.y, here.z), Vector3(to.x, to.y, to.z));
        uint32 const type = path.getPathType();
        if ((type & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)) || path.getPath().size() < 2)
            return false;
        return path.Length() < ZERO_PATH_YD;
    }

    bool FindExit(Player* me, Spot const& to, Spot& out)
    {
        Map const* map = me->GetMap();
        Spot const here = Here(me);
        for (float radius : RINGS)
        {
            for (uint32 i = 0; i < ANGLES; ++i)
            {
                float const angle = 2.0f * float(M_PI) * float(i) / float(ANGLES);
                Spot s;
                s.x = here.x + std::cos(angle) * radius;
                s.y = here.y + std::sin(angle) * radius;
                s.z = map->GetHeight(s.x, s.y, here.z + MAX_STEP_HEIGHT_YD);
                if (s.z <= INVALID_HEIGHT || std::fabs(s.z - here.z) > MAX_STEP_HEIGHT_YD)
                    continue;
                // (walls only: the object it is caught in is in every direction)
                if (!map->isInLineOfSight(here.x, here.y, here.z + 2.0f, s.x, s.y, s.z + 2.0f, false))
                    continue;
                float length = 0.0f;
                if (!Reaches(me, s, to, &length))
                    continue;
                if (Distance2d(s, to) > TRAP_MIN_YD && length < ZERO_PATH_YD)
                    continue;   // caught in the object here too
                out = s;
                return true;
            }
        }
        return false;
    }

    bool HopIfTrapped(Player* me, Spot const& to)
    {
        if (!Trapped(me, to))
            return false;
        Spot exit;
        if (!FindExit(me, to, exit))
            return false;
        me->NearTeleportTo(exit.x, exit.y, exit.z, me->GetOrientation());
        return true;
    }
}
}
}
