#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "WaypointManager.h"
#include "WaypointMovementGenerator.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace PatrolTracker
{
    namespace
    {
        // A leg of the predicted walk: from `from` at `startSec` to `to` at `endSec`.
        // Waits are legs with from == to.
        struct Leg
        {
            Position3 from;
            Position3 to;
            float startSec = 0.0f;
            float endSec = 0.0f;
        };

        // Bounds the walk over a path whose nodes are all on one spot and have no delay.
        uint32 const MAX_LEGS = 256;
        float const MIN_SPEED = 0.5f;   // yd/s, a creature with speed 0 would never leave

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        Position3 NodePosition(WaypointNode const& node)
        {
            Position3 p;
            p.x = node.x;
            p.y = node.y;
            p.z = node.z;
            return p;
        }

        float SpeedOf(Creature const* creature)
        {
            // WaypointMovementGenerator::StartMove walks unless UNIT_STATE_RUNNING is set.
            float const speed = creature->GetSpeed(creature->IsWalking() ? MOVE_WALK : MOVE_RUN);
            return std::max(speed, MIN_SPEED);
        }

        void AddMove(std::vector<Leg>& legs, Position3 const& from, Position3 const& to, float speed, float& t)
        {
            Leg leg;
            leg.from = from;
            leg.to = to;
            leg.startSec = t;
            t += PackResolver::Distance(from, to) / speed;
            leg.endSec = t;
            legs.push_back(leg);
        }

        void AddWait(std::vector<Leg>& legs, Position3 const& at, float seconds, float& t)
        {
            if (seconds <= 0.0f)
                return;
            Leg leg;
            leg.from = at;
            leg.to = at;
            leg.startSec = t;
            t += seconds;
            leg.endSec = t;
            legs.push_back(leg);
        }

        std::vector<TrackPoint> Sample(std::vector<Leg> const& legs, Position3 const& start, float horizonSec, float stepSec)
        {
            std::vector<TrackPoint> points;
            size_t leg = 0;
            for (float t = 0.0f; t <= horizonSec + 0.001f; t += stepSec)
            {
                while (leg < legs.size() && legs[leg].endSec < t)
                    ++leg;
                TrackPoint p;
                p.atSec = t;
                if (leg >= legs.size())
                    p.pos = legs.empty() ? start : legs.back().to;     // stands at the end of the walk
                else
                {
                    Leg const& l = legs[leg];
                    float const span = l.endSec - l.startSec;
                    float const f = span > 0.0f ? std::min(1.0f, std::max(0.0f, (t - l.startSec) / span)) : 1.0f;
                    p.pos.x = l.from.x + (l.to.x - l.from.x) * f;
                    p.pos.y = l.from.y + (l.to.y - l.from.y) * f;
                    p.pos.z = l.from.z + (l.to.z - l.from.z) * f;
                }
                points.push_back(p);
            }
            return points;
        }

        // The creature's running waypoint path, or null. Same lookup as
        // WaypointMovementGenerator<Creature>::LoadPath (path id 0).
        WaypointMovementGenerator<Creature> const* WaypointGenerator(Creature* creature, WaypointPath const*& path)
        {
            path = nullptr;
            if (creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != WAYPOINT_MOTION_TYPE)
                return nullptr;
            auto const* gen = static_cast<WaypointMovementGenerator<Creature> const*>(creature->GetMotionMaster()->GetCurrent());
            if (!gen)
                return nullptr;
            WaypointPathOrigin origin = PATH_NO_PATH;
            gen->GetPathInformation(origin);
            // PATH_FROM_SPECIAL paths are keyed by values the generator does not expose.
            if (origin != PATH_FROM_GUID && origin != PATH_FROM_ENTRY)
                return nullptr;
            path = sWaypointMgr.GetPathFromOrigin(creature->GetEntry(), creature->GetGUIDLow(), 0, origin);
            if (!path || path->empty() || path->find(gen->GetCurrentNode()) == path->end())
            {
                path = nullptr;
                return nullptr;
            }
            return gen;
        }

        // Walk along the waypoint path from the creature's current position.
        bool PathLegs(Creature* creature, float horizonSec, std::vector<Leg>& legs)
        {
            WaypointPath const* path = nullptr;
            WaypointMovementGenerator<Creature> const* gen = WaypointGenerator(creature, path);
            if (!gen)
                return false;

            float const speed = SpeedOf(creature);
            Position3 pos = PositionOf(creature);
            float t = 0.0f;

            WaypointPath::const_iterator it = path->find(gen->GetCurrentNode());
            // Standing at the node it reached last: waiting there (time left unknown, taken as 0:
            // the early side) or about to start. Its next target is the following node.
            bool const atNode = gen->getLastReachedWaypoint() == gen->GetCurrentNode() &&
                (creature->movespline->Finalized() || creature->IsStopped());
            if (atNode)
            {
                ++it;
                if (it == path->end())
                    it = path->begin();
            }

            float pathLength = 0.0f;    // guard against loops that never advance in time
            for (uint32 n = 0; n < MAX_LEGS && t < horizonSec; ++n)
            {
                Position3 const to = NodePosition(it->second);
                float const before = t;
                AddMove(legs, pos, to, speed, t);
                // A node with a wander distance wanders near it for `delay`: counted as standing there.
                AddWait(legs, to, it->second.delay / 1000.0f, t);
                pathLength += t - before;
                pos = to;
                ++it;
                if (it == path->end())
                {
                    if (pathLength <= 0.0f)
                        break;
                    pathLength = 0.0f;
                    it = path->begin();
                }
            }
            return true;
        }

        bool SplineLegs(Creature* creature, std::vector<Leg>& legs)
        {
            if (creature->movespline->Finalized() || !creature->movespline->Initialized())
                return false;
            G3D::Vector3 const dest = creature->movespline->FinalDestination();
            Position3 to;
            to.x = dest.x;
            to.y = dest.y;
            to.z = dest.z;
            float t = 0.0f;
            AddMove(legs, PositionOf(creature), to, SpeedOf(creature), t);
            return true;
        }
    }

    Track Predict(Creature* creature, float horizonSec, float stepSec)
    {
        Track track;
        if (!creature)
            return track;
        stepSec = std::max(stepSec, 0.1f);
        Position3 const here = PositionOf(creature);
        std::vector<Leg> legs;

        if (PathLegs(creature, horizonSec, legs))
            track.source = TrackSource::Path;
        else if (CreatureGroup* group = creature->GetCreatureGroup())
        {
            // Formation member: follows the leader at roughly its current offset.
            Creature* leader = creature->GetMap()->GetCreature(group->GetLeaderGuid());
            if (leader && leader != creature && leader->IsAlive() && PathLegs(leader, horizonSec, legs))
            {
                Position3 const lead = PositionOf(leader);
                float const dx = here.x - lead.x;
                float const dy = here.y - lead.y;
                float const dz = here.z - lead.z;
                for (Leg& l : legs)
                {
                    l.from.x += dx; l.from.y += dy; l.from.z += dz;
                    l.to.x += dx;   l.to.y += dy;   l.to.z += dz;
                }
                track.source = TrackSource::Leader;
            }
        }
        if (legs.empty() && SplineLegs(creature, legs))
            track.source = TrackSource::Spline;

        track.points = Sample(legs, here, horizonSec, stepSec);
        if (!track.points.empty())
            track.points.front().pos = here;
        return track;
    }

    Approach ClosestApproach(Track const& track, Position3 const& point, float radius)
    {
        Approach a;
        if (track.points.empty())
            return a;
        a.minDistance = PackResolver::Distance(track.points.front().pos, point);
        a.minAtSec = 0.0f;
        a.alwaysWithin = true;
        bool inside = false;
        for (TrackPoint const& p : track.points)
        {
            float const d = PackResolver::Distance(p.pos, point);
            if (d < a.minDistance)
            {
                a.minDistance = d;
                a.minAtSec = p.atSec;
            }
            if (d <= radius)
            {
                if (!a.comesWithin)
                {
                    a.comesWithin = true;
                    a.enterSec = p.atSec;
                }
                inside = true;
            }
            else
            {
                a.alwaysWithin = false;
                if (inside)
                {
                    inside = false;
                    a.clearSec = p.atSec;
                }
            }
        }
        if (inside)
            a.clearSec = track.points.back().atSec;
        return a;
    }

    float FirstClearWindow(Track const& track, std::vector<Zone> const& zones, float windowSec)
    {
        if (track.points.empty())
            return -1.0f;
        // Latest sample inside any zone so far; a window starting after it is clear up to the
        // current sample.
        float const step = track.points.size() > 1 ? track.points[1].atSec - track.points[0].atSec : SAMPLE_STEP_SEC;
        float lastInside = -1.0f;
        bool any = false;
        for (TrackPoint const& p : track.points)
        {
            for (Zone const& z : zones)
            {
                if (PackResolver::Distance(p.pos, z.center) <= z.radius)
                {
                    lastInside = p.atSec;
                    any = true;
                    break;
                }
            }
            float const start = any ? lastInside + step : 0.0f;
            if (p.atSec - start >= windowSec)
                return start;
        }
        return -1.0f;
    }

    char const* SourceName(TrackSource source)
    {
        switch (source)
        {
            case TrackSource::Standing: return "standing";
            case TrackSource::Spline:   return "moving";
            case TrackSource::Path:     return "path";
            case TrackSource::Leader:   return "follows leader";
        }
        return "?";
    }
}
}
