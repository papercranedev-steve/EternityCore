/*
 * PartyBot Commander layer 1 (eyes): when the moments of a pull come if the tank went now
 * (user 2026-11-24: casters and healers can go on drinking while the tank walks, opens and
 * builds threat; the ready check should count that time instead of waiting for everyone's
 * mana before the tank moves). Pure. Seconds from the tank's go (Approach starts).
 *
 *   walkSec     the tank's walk to where it opens: path / RUN_YARDS_PER_SEC. Walk-in: to the
 *               target, less MELEE_REACH; the navmesh path when known (RouteFact), else the
 *               straight line x PATH_FACTOR; a charging warrior to CHARGE_MAX_YD. Shot /
 *               face / ambush: to that spot.
 *   openSec     the pull starts: walk-in on arrival; shot + OPENER_SEC (cast, flight); face +
 *               FACE_NOTICE_SEC; ambush the later of the walk and the patrol's predicted wait.
 *   contactSec  the pack is on the tank where the party fights (the healers' first heals):
 *               walk-in on arrival; opened from afar and fought there: + the target's run
 *               to the tank at ENEMY_RUN_YARDS_PER_SEC; brought back: the later of the tank's
 *               walk back at BACK_YARDS_PER_SEC and the pack's run to the fight spot.
 *   damageSec   damage dealers may start: contact + THREAT_LEAD_SEC (ThreatRoom holds them
 *               until the tank has threat; that has no forecast yet).
 *
 * Not counted: a line of sight pull (Settle, casters standing back), an opening control's
 * walk, the patrol hold, pathing detours of a shot spot walk. Constants untested: to be
 * compared with the combat recordings before anything waits on them.
 */

#ifndef MANGOS_COMMANDER_PULL_TIMELINE_H
#define MANGOS_COMMANDER_PULL_TIMELINE_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
namespace PullTimeline
{
    float const RUN_YARDS_PER_SEC = 7.0f;
    float const BACK_YARDS_PER_SEC = 4.5f;          // = PullAssigner::RETURN_BACK_SPEED
    float const ENEMY_RUN_YARDS_PER_SEC = 6.0f;     // = PullAssigner::ARRIVAL_SPEED
    float const PATH_FACTOR = 1.3f;                 // straight line to path (RouteGuard, PatrolHold)
    float const MELEE_REACH = 5.0f;
    float const OPENER_SEC = 1.0f;
    float const FACE_NOTICE_SEC = 1.0f;
    float const THREAT_LEAD_SEC = 3.0f;
    // A walk-in warrior charges when the pack is fought where it stands (PullMethod.h): the
    // walk ends this far from the target, the flight counted as no time (it errs early, the
    // safe side for the healers' and casters' rest).
    float const CHARGE_MAX_YD = 25.0f;

    enum class Opener { WalkIn, Shot, Face, Ambush };

    struct Input
    {
        Opener opener = Opener::WalkIn;
        Position3 tankAt;           // now
        Position3 openFrom;         // walk-in: the target; else the shot / face / ambush spot
        float pathYd = -1.0f;       // the tank's navmesh walk to openFrom, < 0 = unknown
        bool charges = false;       // walk-in: the tank charges the last CHARGE_MAX_YD
        Position3 packAt;           // the target (ambush: where the patrol meets the tank)
        float ambushWaitSec = 0.0f; // ambush: the patrol comes by after this long
        bool returnToSpot = false;  // the tank brings the pack back to fightSpot
        Position3 fightSpot;
    };

    struct Result
    {
        float walkSec = 0.0f;
        float openSec = 0.0f;
        float contactSec = 0.0f;
        float damageSec = 0.0f;
    };

    Result Of(Input const& in);

    // ---- Where the party is, when (design: .kiro/docs/2026-11-25-pull-patrol-timeline.md) ----
    //
    // The pull as places with time windows (seconds from the tank's go), for the one check that
    // judges patrols (PullPatrolCheck::ChooseStart): a patrol joins when it is at a place while
    // the party is there. Before, the check looked at the pulled creatures' spots and the
    // party's spot for the whole fight and never at the tank's walk: a patrol crossing the walk
    // was seen by nobody, one passing a spot the pack had left long ago (brought back) counted
    // as joining, with the creatures it would call there.
    //
    //   Walk   the tank's walk to openFrom, a point every WALK_SAMPLE_YD at its time on the walk
    //          (walkSec spread over the path), +- WALK_WINDOW_SEC. Radius: the patrol's aggro
    //          range against the tank + the hard line (it only walks past; user 2026-11-25 D3).
    //   Place  the tank and the pack: the pulled creatures' spots until PACK_LEAVE_SEC after
    //          the opener; the open spot (shot, face, ambush) from walkSec; brought back: the
    //          walk back (points as for Walk, at BACK_YARDS_PER_SEC) and the fight spot from
    //          the tank's arrival there; fought where they stand: the pack's spot (walk-in,
    //          ambush: the meeting point) or the open spot (shot, face) to the end.
    //          Radius: max(PATROL_MIN_CLEARANCE, aggro range against the tank) (the caller's).
    //   Party  the party's spot (`partyAt`, where the leader stands) the whole time. Radius:
    //          max(PATROL_MIN_CLEARANCE, aggro range against the leader).
    enum class ZoneKind : uint8 { Walk, Place, Party };

    struct Zone
    {
        ZoneKind kind = ZoneKind::Place;
        Position3 center;
        float fromSec = 0.0f;
        float toSec = 0.0f;             // FOREVER: to the end of the fight
        char const* what = "";          // "walk", "pack", "open spot", "walk back", "fight", "party"
    };

    float const FOREVER = 1.0e9f;
    float const WALK_SAMPLE_YD = 3.0f;
    float const WALK_WINDOW_SEC = 0.5f;     // = PatrolTracker::SAMPLE_STEP_SEC
    float const PACK_LEAVE_SEC = 2.0f;

    // `walk`: the tank's navmesh walk from in.tankAt to in.openFrom (empty: the straight line).
    // `packSpots`: where the pulled creatures stand now.
    std::vector<Zone> Presence(Input const& in, Result const& r, std::vector<Position3> const& walk,
                               std::vector<Position3> const& packSpots, Position3 const& partyAt);

    char const* KindName(ZoneKind kind);
}
}

#endif
