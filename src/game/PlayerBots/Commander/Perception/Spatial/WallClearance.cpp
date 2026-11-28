#include "Commander/Perception/Spatial/WallClearance.h"
#include "Map.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "Player.h"
#include "Detour/Include/DetourNavMeshQuery.h"
#include <cmath>

namespace Commander
{
namespace WallClearance
{
    namespace
    {
        // findNearestPoly half extents (Detour order: x, height, y).
        float const EXTENTS[3] = { 1.0f, 2.0f, 1.0f };
        int const MAX_VISITED = 16;
        // A step must gain this much room to be worth it.
        float const MIN_GAIN_YD = 0.1f;

        struct Query
        {
            dtNavMeshQuery const* query = nullptr;
            dtQueryFilter filter;
        };

        bool QueryOf(Player const* me, Query& q)
        {
            if (me->GetTransport())
                return false;
            q.query = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(me->GetMapId());
            if (!q.query)
                return false;
            q.filter.setIncludeFlags(NAV_GROUND);
            q.filter.setExcludeFlags(NAV_STEEP_SLOPES);
            return true;
        }

        // The room at a mesh point `pos` (Detour order) on polygon `ref`, up to `maxYd`.
        float RoomAt(Query const& q, dtPolyRef ref, float const* pos, float maxYd)
        {
            float dist = 0.0f, hitPos[3], hitNormal[3];
            if (dtStatusFailed(q.query->findDistanceToWall(ref, pos, maxYd, &q.filter, &dist, hitPos, hitNormal)))
                return -1.0f;
            return dist;
        }

        bool OnMesh(Query const& q, Player const* me, dtPolyRef& ref, float* pos)
        {
            float const at[3] = { me->GetPositionY(), me->GetPositionZ(), me->GetPositionX() };
            return !dtStatusFailed(q.query->findNearestPoly(at, EXTENTS, &q.filter, &ref, pos)) && ref;
        }
    }

    float Of(Player const* me, float maxYd)
    {
        Query q;
        dtPolyRef ref = 0;
        float pos[3];
        if (!QueryOf(me, q) || !OnMesh(q, me, ref, pos))
            return -1.0f;
        return RoomAt(q, ref, pos, maxYd);
    }

    bool StepClear(Player const* me, float wantYd, Spot& out)
    {
        if (me->IsInWater())
            return false;
        Query q;
        dtPolyRef ref = 0;
        float pos[3];
        if (!QueryOf(me, q) || !OnMesh(q, me, ref, pos))
            return false;
        float const here = RoomAt(q, ref, pos, wantYd);
        if (here < 0.0f || here >= wantYd)
            return false;

        // Around the bot along the mesh (moveAlongSurface stops at its edge: no step through
        // the wall), the shortest step with the room, else the most room.
        float best = here + MIN_GAIN_YD;
        bool found = false;
        for (float step : STEP_YD)
        {
            for (uint32 i = 0; i < STEP_DIRS; ++i)
            {
                float const a = 2.0f * float(M_PI) * float(i) / float(STEP_DIRS);
                float const end[3] = { pos[0] + step * std::sin(a), pos[1], pos[2] + step * std::cos(a) };
                float result[3];
                dtPolyRef visited[MAX_VISITED];
                int visitedCount = 0;
                if (dtStatusFailed(q.query->moveAlongSurface(ref, pos, end, &q.filter, result, visited, &visitedCount, MAX_VISITED)) ||
                    visitedCount <= 0)
                    continue;
                dtPolyRef const at = visited[visitedCount - 1];
                float h = result[1];
                q.query->getPolyHeight(at, result, &h);
                result[1] = h;
                float const room = RoomAt(q, at, result, wantYd);
                if (room < best)
                    continue;
                best = room;
                found = true;
                out.x = result[2];
                out.y = result[0];
                out.z = result[1];
            }
            if (found && best >= wantYd)
                return true;
        }
        return found;
    }
}
}
