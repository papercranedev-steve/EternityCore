#include "Commander/Behaviors/Movement/FollowSpot.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
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

        // The party's position field, while one is built (PositionFieldBuilder: also while
        // travelling near creatures that could attack us), and how long we stay.
        struct FieldView
        {
            std::shared_ptr<PartyCommander> commander;     // keeps the field alive
            WorldModel const* world = nullptr;
            PositionField const* field = nullptr;
            float staySec = 0.0f;
        };

        FieldView ViewOf(Player const* me)
        {
            FieldView v;
            // (the commander of the bot's own map: this map thread)
            v.commander = sCommanderMgr.FindCommander(me);
            if (!v.commander)
                return v;
            WorldModel const& world = v.commander->GetWorld();
            v.world = &world;
            v.field = world.field.get();
            v.staySec = RouteGuard::StaySec(world, v.commander->GetPlan());
            return v;
        }

        // The field's facts at `p`: no idle creature covers its cell and no patrol comes there
        // before we leave (PatrolZone). Outside the field (or none): true, the exact checks decide.
        bool FieldClear(FieldView const& v, Position3 const& p)
        {
            if (!v.field)
                return true;
            FieldCell const* c = v.field->At(p);
            if (!c || !c->standable)
                return true;
            if (c->idle)
                return false;
            return !PatrolZone::Comes(*v.world, p, v.staySec);
        }

        // Where the bot stops (leader now) and the follow points ahead on the leader track.
        bool PointsAreSafe(Player const* me, Unit const* leader, std::vector<LeaderAt> const& track,
                           SafeRoute::IdleCreatures const& idle, FieldView const& view, Offset const& o)
        {
            Position3 const stop = PointAt(me, leader, track.front(), o);
            if (!FieldClear(view, stop) || !SpotIsSafe(idle, stop))
                return false;
            for (size_t i = 1; i < track.size(); ++i)
            {
                Position3 const p = PointAt(me, leader, track[i], o);
                if (!FieldClear(view, p) || idle.Near(p))
                    return false;
            }
            return true;
        }

        // Offsets to try when the current one is not safe, best first. With a field: its
        // cells CELL_MIN_DIST..CELL_MAX_DIST from the leader that are clear (FieldClear,
        // standable), closest to straight behind first. Without: the fixed CANDIDATE_STEPS x
        // CANDIDATE_DISTS behind the leader.
        std::vector<Offset> Candidates(Player const* me, Unit const* leader, FieldView const& view)
        {
            std::vector<Offset> out;
            float const lx = leader->GetPositionX(), ly = leader->GetPositionY(), lo = leader->GetOrientation();
            if (view.field)
            {
                struct Cell
                {
                    Offset o;
                    float gap;
                };
                std::vector<Cell> cells;
                float const radii = leader->GetObjectBoundingRadius() + me->GetObjectBoundingRadius();
                int32 const cx0 = PositionField::CellOf(lx - CELL_MAX_DIST), cx1 = PositionField::CellOf(lx + CELL_MAX_DIST);
                int32 const cy0 = PositionField::CellOf(ly - CELL_MAX_DIST), cy1 = PositionField::CellOf(ly + CELL_MAX_DIST);
                for (int32 cx = cx0; cx <= cx1; ++cx)
                    for (int32 cy = cy0; cy <= cy1; ++cy)
                    {
                        FieldCell const* c = view.field->At(cx, cy);
                        if (!c || !c->standable)
                            continue;
                        Position3 const p = view.field->CenterOf(cx, cy);
                        if (!FieldClear(view, p))
                            continue;
                        float const dx = p.x - lx, dy = p.y - ly;
                        float const d = std::sqrt(dx * dx + dy * dy);
                        if (d < CELL_MIN_DIST || d > CELL_MAX_DIST)
                            continue;
                        Cell k;
                        k.o.dist = std::max(0.0f, d - radii);
                        k.o.angle = std::atan2(dy, dx) - lo;
                        k.gap = AngleGap(k.o.angle, float(M_PI));
                        cells.push_back(k);
                    }
                std::sort(cells.begin(), cells.end(), [](Cell const& a, Cell const& b)
                {
                    return a.gap != b.gap ? a.gap < b.gap : a.o.dist < b.o.dist;
                });
                for (Cell const& k : cells)
                    out.push_back(k.o);
                return out;
            }
            for (float step : CANDIDATE_STEPS)
                for (float dist : CANDIDATE_DISTS)
                    out.push_back(Offset{ dist, float(M_PI) + step });
            return out;
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
        FieldView const view = ViewOf(me);
        SafeRoute::IdleCreatures const idle(me, leader, FOLLOW_MARGIN);
        // (a patrol far off is not an idle creature near us yet, but its route may be)
        if (idle.Empty() && !view.field)
            return current;
        std::vector<LeaderAt> const track = LeaderTrack(leader, lookaheadSec);
        uint32 pathChecks = 0;
        if (PointsAreSafe(me, leader, track, idle, view, current) &&
            WalkIsSafe(me, leader, track, idle, current, pathChecks))
            return current;

        // Behind the leader first, then turning to the sides: the leader has just walked
        // there, ahead nobody has looked yet.
        uint32 tried = 0;
        for (Offset const& o : Candidates(me, leader, view))
        {
            if (pathChecks >= MAX_PATH_CHECKS || tried >= MAX_CANDIDATES)
                break;
            ++tried;
            if (PointsAreSafe(me, leader, track, idle, view, o) && WalkIsSafe(me, leader, track, idle, o, pathChecks))
                return o;
        }
        return Offset{ FALLBACK_DIST, float(M_PI) };
    }

    bool NeedsRecheck(Player const* me, Unit const* leader, Offset const& current)
    {
        FieldView const view = ViewOf(me);
        return view.field && !FieldClear(view, PointOf(me, leader, current));
    }
}
}
