/*
 * PartyBot Commander layer 1 (eyes): where the tank can wait for a patrol on its way, and when
 * the patrol comes by (230431: the tank walked after a patrolling gargoyle and followed it 70 yd
 * into an idle pack; a creature that walks is pulled where it will be, not where it is).
 *
 * Find: the patrol's predicted track (PatrolFacts::track, PatrolZone's clock), earliest point
 * first, up to Request::maxWaitSec. A track point is the stand point (on the ground below it)
 * when
 *  - it lies within Request::maxFromNearYd of Request::partyAt (the party: the fight is there);
 *  - its ground is within the patrol's aggro reach - AGGRO_INSIDE of the point (a flyer high
 *    above the ground does not notice the tank);
 *  - the patrol comes within that distance of it (`meetAt`, `arriveSec`) after the tank got
 *    there: the straight walk x PATH_FACTOR at RUN_YD_PER_SEC + LEAD_SEC, and again with the
 *    real path length;
 *  - no idle creature would attack the tank standing there (SafeRoute::IdleCreatures, the
 *    patrol and Request::ignore, its group, left out);
 *  - no other patrol comes there (PatrolZone::ArriveSec) before the patrol does, + LEAD_SEC;
 *  - the patrol sees it from `meetAt`;
 *  - the walk there is safe (SafeRoute::PathIsSafe, at most MAX_DETOUR).
 * Track points closer than MIN_STEP_YD to the last one checked are skipped; at most MAX_PATHS
 * path searches per call.
 *
 * Facts only: PullChoice decides whether an ambush is the pull, PullBehavior walks there.
 * Reads live creatures and pathfinds: map thread, or the world thread while maps are idle.
 */

#ifndef MANGOS_COMMANDER_AMBUSH_SPOT_H
#define MANGOS_COMMANDER_AMBUSH_SPOT_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;
class Map;
class Player;

namespace Commander
{
namespace AmbushSpot
{
    float const AGGRO_INSIDE = 4.0f;
    float const RUN_YD_PER_SEC = 7.0f;
    float const PATH_FACTOR = 1.3f;
    float const LEAD_SEC = 2.0f;
    float const MAX_DETOUR = 1.8f;
    float const MIN_STEP_YD = 3.0f;
    float const UNIT_HEIGHT = 2.0f;
    uint32 const MAX_PATHS = 6;

    struct Request
    {
        Creature* patrol = nullptr;
        PatrolFacts const* facts = nullptr;     // the patrol's facts (track)
        Player* tank = nullptr;                 // walks there from where it stands now
        Position3 partyAt;                      // where the party is (not `near`: a Windows macro)
        float maxFromNearYd = 0.0f;
        float maxWaitSec = 0.0f;                // from the world refresh
        std::vector<ObjectGuid> ignore;         // the patrol's group (comes with it)
    };

    // Why no spot was found: the furthest check some track point reached.
    enum class Failure : uint8
    {
        None,
        NoTrack,    // no predicted track (or the patrol does not count, PatrolZone::Counts)
        TooFar,     // its way within maxWaitSec stays away from the party
        NoGround,   // no ground near enough below the points left
        Late,       // the patrol passes the points left before the tank could be there
        Aggro,      // the points left are inside an idle creature's aggro range
        Patrol,     // another patrol comes to the points left first
        NoSight,    // the patrol would not see the tank on the points left
        NoPath,     // no safe walk to the points left
        Budget,     // path search budget used up
    };

    struct Spot
    {
        Position3 at;           // where the tank stands
        Position3 meetAt;       // where the patrol is when it notices the tank
        float arriveSec = 0.0f; // when, from the world refresh
        float walkYd = 0.0f;    // the tank's path there
    };

    bool Find(WorldModel const& world, Map* map, Request const& request, Spot& out, Failure* why = nullptr);
    char const* FailureName(Failure failure);
}
}

#endif
