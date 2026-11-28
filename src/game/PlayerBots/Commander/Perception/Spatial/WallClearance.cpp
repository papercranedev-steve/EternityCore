#include "Commander/Perception/Spatial/WallClearance.h"
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

        bool QueryOf(uint32 mapId, Query& q)
        {
            q.query = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(mapId);
            if (!q.query)
                return false;
            q.filter.setIncludeFlags(NAV_GROUND);
            q.filter.setExcludeFlags(NAV_STEEP_SLOPES);
            return true;
        }

        // The room at a mesh point `pos` (Detour order) on polygon `ref`, up to `maxYd`.
        float RoomOn(Query const& q, dtPolyRef ref, float const* pos, float maxYd)
        {
            float dist = 0.0f, hitPos[3], hitNormal[3];
            if (dtStatusFailed(q.query->findDistanceToWall(ref, pos, maxYd, &q.filter, &dist, hitPos, hitNormal)))
                return -1.0f;
            return dist;
        }

        bool OnMesh(Query const& q, Spot const& s, dtPolyRef& ref, float* pos)
        {
            float const at[3] = { s.y, s.z, s.x };
            return !dtStatusFailed(q.query->findNearestPoly(at, EXTENTS, &q.filter, &ref, pos)) && ref;
        }
    }

    float RoomAt(uint32 mapId, Spot const& at, float maxYd)
    {
        Query q;
        dtPolyRef ref = 0;
        float pos[3];
        if (!QueryOf(mapId, q) || !OnMesh(q, at, ref, pos))
            return -1.0f;
        return RoomOn(q, ref, pos, maxYd);
    }

    bool StepClear(uint32 mapId, Spot const& from, float wantYd, Spot& out)
    {
        Query q;
        dtPolyRef ref = 0;
        float pos[3];
        if (!QueryOf(mapId, q) || !OnMesh(q, from, ref, pos))
            return false;
        float const here = RoomOn(q, ref, pos, wantYd);
        if (here < 0.0f || here >= wantYd)
            return false;

        // Around it along the mesh (moveAlongSurface stops at its edge), the shortest step
        // with the room, else the most room.
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
                float const room = RoomOn(q, at, result, wantYd);
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

    bool StepClear(Player const* me, float wantYd, Spot& out)
    {
        if (me->GetTransport() || me->IsInWater())
            return false;
        Spot const here{ me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() };
        return StepClear(me->GetMapId(), here, wantYd, out);
    }
}
}
