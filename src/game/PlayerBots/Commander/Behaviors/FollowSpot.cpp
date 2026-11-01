#include "Commander/Behaviors/FollowSpot.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Map.h"
#include "MoveSpline.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Commander
{
namespace FollowSpot
{
    namespace
    {
        struct LeaderAt
        {
            float x, y, z, o;
        };

        // Where the leader is now and, while it moves, every LEAD_STEP_SEC along its facing
        // up to `lookaheadSec` (the follow generator runs ahead of a moving leader too).
        std::vector<LeaderAt> LeaderTrack(Unit const* leader, float lookaheadSec)
        {
            std::vector<LeaderAt> track;
            LeaderAt const now{ leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), leader->GetOrientation() };
            track.push_back(now);
            // (movement flags: a client; spline: a character moved by the server)
            if (!leader->IsMoving() && leader->movespline->Finalized())
                return track;
            float const speed = leader->GetSpeed(leader->IsWalking() ? MOVE_WALK : MOVE_RUN);
            for (float t = LEAD_STEP_SEC; t <= lookaheadSec + 0.01f; t += LEAD_STEP_SEC)
            {
                LeaderAt p = now;
                p.x += speed * t * std::cos(now.o);
                p.y += speed * t * std::sin(now.o);
                track.push_back(p);
            }
            return track;
        }

        Position3 PointAt(Player const* me, Unit const* leader, LeaderAt const& at, Offset const& o)
        {
            // Same as WorldObject::GetNearPointAroundPosition: offset plus both bounding radii.
            float const d = o.dist + leader->GetObjectBoundingRadius() + me->GetObjectBoundingRadius();
            float const a = at.o + o.angle;
            Position3 p;
            p.x = at.x + d * std::cos(a);
            p.y = at.y + d * std::sin(a);
            // Ground at the point: the eye height there decides the line of sight (Stockade:
            // the corridor floor is 1 yd above the cell floor the leader stood on).
            float const ground = me->GetMap()->GetHeight(p.x, p.y, at.z + GROUND_SEARCH_UP);
            p.z = (ground > INVALID_HEIGHT && std::fabs(ground - at.z) <= MAX_GROUND_STEP) ? ground : at.z;
            return p;
        }

        // The point and SPOT_SPREAD around it: line of sight changes within a step (a cell door),
        // and the bot never stops exactly on the computed point.
        bool SpotIsSafe(SafeRoute::IdleCreatures const& idle, Position3 const& p)
        {
            if (idle.Near(p))
                return false;
            static float const dirs[4][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
            for (auto const& d : dirs)
            {
                Position3 q = p;
                q.x += d[0] * SPOT_SPREAD;
                q.y += d[1] * SPOT_SPREAD;
                if (idle.Near(q))
                    return false;
            }
            return true;
        }

        // Where the bot stops (leader now) and the follow points ahead on the leader track.
        bool PointsAreSafe(Player const* me, Unit const* leader, std::vector<LeaderAt> const& track,
                           SafeRoute::IdleCreatures const& idle, Offset const& o)
        {
            if (!SpotIsSafe(idle, PointAt(me, leader, track.front(), o)))
                return false;
            for (size_t i = 1; i < track.size(); ++i)
                if (idle.Near(PointAt(me, leader, track[i], o)))
                    return false;
            return true;
        }

        // The walk from the bot to the current follow point: a straight line when short, else
        // the navmesh path (the follow generator walks it). `pathChecks` counts pathfinding.
        bool WalkIsSafe(Player* me, Unit const* leader, std::vector<LeaderAt> const& track,
                        SafeRoute::IdleCreatures const& idle, Offset const& o, uint32& pathChecks)
        {
            Position3 const to = PointAt(me, leader, track.front(), o);
            Position3 here;
            here.x = me->GetPositionX();
            here.y = me->GetPositionY();
            here.z = me->GetPositionZ();
            if (SafeRoute::Distance2d(here, to) <= STRAIGHT_WALK_DIST)
                return SafeRoute::SegmentIsSafe(idle, here, to);
            ++pathChecks;
            return SafeRoute::WalkIsSafe(me, idle, to);
        }
    }

    float AngleGap(float a, float b)
    {
        float const d = std::fmod(std::fabs(a - b), 2.0f * float(M_PI));
        return d > float(M_PI) ? 2.0f * float(M_PI) - d : d;
    }

    Position3 PointOf(Player const* me, Unit const* leader, Offset const& o)
    {
        LeaderAt const at{ leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), leader->GetOrientation() };
        return PointAt(me, leader, at, o);
    }

    Offset Choose(Player* me, Unit const* leader, Offset const& current, float lookaheadSec)
    {
        SafeRoute::IdleCreatures const idle(me, leader, FOLLOW_MARGIN);
        if (idle.Empty())
            return current;
        std::vector<LeaderAt> const track = LeaderTrack(leader, lookaheadSec);
        uint32 pathChecks = 0;
        if (PointsAreSafe(me, leader, track, idle, current) &&
            WalkIsSafe(me, leader, track, idle, current, pathChecks))
            return current;

        // Behind the leader only, straight behind first, then nearer the leader: the leader has
        // just walked there, ahead nobody has looked yet.
        std::vector<Offset> candidates;
        for (float step : CANDIDATE_STEPS)
            for (float dist : CANDIDATE_DISTS)
                candidates.push_back(Offset{ dist, float(M_PI) + step });

        for (Offset const& o : candidates)
        {
            if (pathChecks >= MAX_PATH_CHECKS)
                break;
            if (PointsAreSafe(me, leader, track, idle, o) && WalkIsSafe(me, leader, track, idle, o, pathChecks))
                return o;
        }
        return Offset{ FALLBACK_DIST, float(M_PI) };
    }
}
}
