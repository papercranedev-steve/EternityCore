/*
 * PartyBot Commander layer 1: where a patrolling creature will walk in the next seconds.
 * Design: .kiro/docs/2026-11-01-pull-planning.md (patrol waiting), combat scenarios 4, 11, 13.
 *
 * Reads live objects (motion master, waypoint path, move spline): map thread, or the world
 * thread while maps are idle (GM commands).
 *
 * Track sources, best first:
 *  - Path:      the creature's running WaypointMovementGenerator. The path comes from
 *               sWaypointMgr (creature_movement by guid, else creature_movement_template by
 *               entry, the same lookup WaypointMovementGenerator::LoadPath does). Walks node to
 *               node in a straight line at the current walk or run speed, waits `delay` ms at
 *               nodes, wraps to the first node at the end (repeating paths).
 *  - Leader:    a creature_groups member following a leader that walks a path: the leader's
 *               track shifted by the member's current offset to the leader.
 *  - Spline:    anything else that moves: the current move spline's final destination at the
 *               current speed, then standing there.
 *  - Standing:  not moving: stays where it is.
 *
 * Unknown parts are filled in on the early side (a creature waiting at a node is assumed to
 * leave now): the planner asks "can it come close", earlier is the safe answer. The planner
 * asks again on every refresh, so a creature that is still waiting is simply seen again.
 */

#ifndef MANGOS_COMMANDER_PATROL_TRACKER_H
#define MANGOS_COMMANDER_PATROL_TRACKER_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;

namespace Commander
{
namespace PatrolTracker
{
    enum class TrackSource : uint8
    {
        Standing,
        Spline,
        Path,
        Leader,
    };

    struct TrackPoint
    {
        float atSec = 0.0f;     // seconds from now
        Position3 pos;
    };

    struct Track
    {
        TrackSource source = TrackSource::Standing;
        std::vector<TrackPoint> points;     // points[0] is the current position at 0 s
    };

    float const SAMPLE_STEP_SEC = 0.5f;

    Track Predict(Creature* creature, float horizonSec, float stepSec = SAMPLE_STEP_SEC);

    // Closest approach of a track to a point, and how long it stays within `radius`.
    struct Approach
    {
        float minDistance = 0.0f;
        float minAtSec = 0.0f;
        bool comesWithin = false;   // some sample within `radius`
        float enterSec = 0.0f;      // first sample within `radius`
        // From this time on every sample is outside `radius`. Equal to the horizon when the
        // last sample is still inside (it leaves later, or never).
        float clearSec = 0.0f;
        bool alwaysWithin = false;  // every sample within `radius`
    };
    Approach ClosestApproach(Track const& track, Position3 const& point, float radius);

    struct Zone
    {
        Position3 center;
        float radius = 0.0f;
    };
    // Earliest time from now (seconds) after which the track stays outside every zone for
    // `windowSec`: a fight starting then is not joined by this creature. 0 = clear now,
    // -1 = no such window within the track (predict at least windowSec beyond the wait you
    // are willing to make). Uses the track's own sample step.
    float FirstClearWindow(Track const& track, std::vector<Zone> const& zones, float windowSec);

    char const* SourceName(TrackSource source);
}
}

#endif
