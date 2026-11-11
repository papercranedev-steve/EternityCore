/*
 * PartyBot Commander layer 1 (eyes): the one rule for "an idle patrol comes here" (user
 * 2026-11-11, doc .kiro/docs/2026-11-10-position-field.md 17). Every check on WorldModel::patrols
 * asks this: the position field's patrol layer, the patrol move and guard (PatrolAssigner,
 * PatrolPlanner), the route guard and its walk off (RouteGuard, OffRouteBehavior), following
 * (FollowSpot), the line of sight pull point (PullLosSearch), melee chase angles
 * (MeleeStanceBehavior), the area spell spot (AreaDamageBehavior) and waking a control
 * (ControlledEnd).
 *
 * Before, the field marked cells by their centre and subtracted the age of the tracks, the
 * others measured the exact point from track[0] (up to a second late) with their own filters
 * and margins: a melee spot near the edge of a zone could be "on the route" for one module and
 * "clear" for another, and the bot went back and forth.
 *
 * Rule:
 *   counts    an aggressive patrol (not neutral, not CREATURE_FLAG_EXTRA_NO_AGGRO, not tapped
 *             by someone else) with a predicted track (PatrolFacts::track);
 *   zone      its aggro reach (PatrolFacts::reach) + MARGIN around each track point, 3D;
 *   where     the centre of the field cell of the point (PositionField::CELL_YD grid, also
 *             outside the field), at the cell's ground height when the field has it standable,
 *             else the point's height: every module asking about the same cell gets the same
 *             answer, and the field's FieldCell::patrolSec is this rule's value at that cell
 *             (a cache, read directly by FieldSpots);
 *   when      seconds from the world refresh (WorldModel::refreshedAtMs) to the first track
 *             point whose zone covers it; the tracks start at the pack refresh
 *             (packsRefreshedAtMs), so their age is subtracted; never below 0;
 *   comes     when >= 0 and when < withinSec (the caller's horizon: how long we stay).
 *
 * Not here (other questions, they read live creatures on the world thread): the pull's patrol
 * timing (PullPatrolCheck: joins the fight within max(PATROL_MIN_CLEARANCE, attack distance)
 * of the pull), the shot and Sap spots (ShotSpot, SapSpot: own live tracks), a walk passing a
 * patrol where it stands now (RouteRisk).
 * Pure (facts only).
 */

#ifndef MANGOS_COMMANDER_PATROL_ZONE_H
#define MANGOS_COMMANDER_PATROL_ZONE_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace PatrolZone
    {
        float const MARGIN = 5.0f;

        bool Counts(PatrolFacts const& p);
        float Radius(PatrolFacts const& p);
        // Seconds since the tracks were predicted, at the world refresh.
        float TrackAgeSec(WorldModel const& world);
        // The point the rule measures for `at` (header: where).
        Position3 Probe(WorldModel const& world, Position3 const& at);
        // Seconds from the refresh for track point `index` (header: when).
        float SecOf(PatrolFacts const& p, size_t index, float ageSec);
        // Where the patrol is `sec` seconds after the refresh (its last track point past the end).
        Position3 PositionAt(WorldModel const& world, PatrolFacts const& p, float sec);

        // When this patrol comes to `at`, -1 = not within its track (or it does not count).
        float ArriveSec(WorldModel const& world, PatrolFacts const& p, Position3 const& at);
        // The first patrol to come to `at`: when, -1 = none; `which` gets it when asked.
        float ArriveSec(WorldModel const& world, Position3 const& at, PatrolFacts const** which = nullptr);

        bool Comes(WorldModel const& world, PatrolFacts const& p, Position3 const& at, float withinSec);
        bool Comes(WorldModel const& world, Position3 const& at, float withinSec);
    }
}

#endif
