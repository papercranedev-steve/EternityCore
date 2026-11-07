/*
 * PartyBot Commander layer 2 helper: a point out of sight of enemy casters standing back.
 * A caster whose main ranged spell fails for line of sight turns its chase back on
 * (CreatureAI::DoSpellsListCasts) and walks to its victim.
 *
 * Used by the line of sight pull (PullPlanner::SearchLosBreak: the tank hides near the fight
 * spot) and by the patrol guard (PatrolPlanner::Step: the victim of a caster left standing in
 * a patrol's path hides, so the caster comes out of the path).
 *
 * FindPoint is map-thread code (line of sight, ground and pathfinding).
 * Candidates: DIRECTIONS rays around the center, first straight away from the first caster
 * (the far side of whatever is between), then turning towards it both ways. Each ray is walked
 * outwards from FIRST_STEP to MAX_DISTANCE every STEP; its first point that no caster sees is
 * the ray's candidate (the nearest corner on that side). A point on the way is skipped when its
 * ground is more than MAX_HEIGHT_DIFF from the center's or the caller's filter rejects it.
 * At most MAX_LOS_CHECKS caster checks per search.
 * The candidates, nearest to the center first, are then checked for the walk (at most
 * MAX_PATHS): every watcher sees the point, the point sees every `sees` place, the mover gets
 * there by a path at most MAX_DETOUR times the straight line that passes no idle creature
 * (SafeRoute::PathIsSafe; a mover that is not a player: straight line of sight instead), and
 * the caller's `accepts` agrees. The first that passes is used.
 *
 * Before 2026-11-03: two rings (6 and 11 yd) x 6 directions, and the mover had to see the point
 * in a straight line (stand-in for a short walk). strat-20261103-010704: both Skeletal
 * Guardians saw all 12 points; the nearest corner hiding the tank from both was 14-18 yd away
 * (a straight line from the tank, but a ring point 60 degrees off), so the tank fought in place.
 * A corner the mover walks around is fine now: the path decides.
 */

#ifndef MANGOS_COMMANDER_LOS_BREAK_H
#define MANGOS_COMMANDER_LOS_BREAK_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"
#include <functional>
#include <vector>

class Creature;
class Map;
class Unit;

namespace Commander
{
    namespace LosBreak
    {
        uint32 const DIRECTIONS = 12;           // 30 degrees apart
        float const FIRST_STEP = 4.0f;
        float const STEP = 2.0f;
        float const MAX_DISTANCE = 18.0f;
        // Ground of a candidate within this of the center's height (no ledges, no other floor).
        float const MAX_HEIGHT_DIFF = 3.0f;
        // Worst case DIRECTIONS x 8 steps x 2 casters = 192; most rays stop at their first hidden point.
        uint32 const MAX_LOS_CHECKS = 160;
        uint32 const MAX_PATHS = 4;
        float const MAX_DETOUR = 1.6f;

        struct Search
        {
            Position3 center;
            std::vector<Creature const*> casters;   // the point hides from each of them (not empty)
            Unit* mover = nullptr;                  // walks there
            std::vector<Unit const*> watchers;      // must still see the point (optional)
            // Optional: the point sees these places (eye height at both ends), e.g. where
            // another bot will stand.
            std::vector<Position3> sees;
            // Optional: true = skip this candidate (idle creatures nearby, a patrol's path).
            std::function<bool(Position3 const&)> rejects;
            // Optional: final check of a candidate that passed everything else (expensive
            // checks such as a creature's path; counted in MAX_PATHS).
            std::function<bool(Position3 const&)> accepts;
        };

        // True and `point` (with its ground height) when a usable point was found.
        bool FindPoint(Map* map, Search const& search, Position3& point);

        // Pure: the creature's profile has a main ranged spell (creature_spells
        // CF_MAIN_RANGED_SPELL): it stands back and casts instead of following its victim.
        bool IsRangedCaster(uint32 entry);
    }
}

#endif
