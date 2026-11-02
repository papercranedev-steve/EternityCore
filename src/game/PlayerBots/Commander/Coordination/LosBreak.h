/*
 * PartyBot Commander layer 2 helper: a point out of sight of enemy casters standing back.
 * A caster whose main ranged spell fails for line of sight turns its chase back on
 * (CreatureAI::DoSpellsListCasts) and walks to its victim.
 *
 * Used by the line of sight pull (PullPlanner::SearchLosBreak: the tank hides near the fight
 * spot) and by the patrol guard (PatrolPlanner::Step: the victim of a caster left standing in
 * a patrol's path hides, so the caster comes out of the path).
 *
 * FindPoint is map-thread code (line of sight and ground checks). Candidates: a ring of
 * TURNS directions x RING_INNER / RING_OUTER around the center, first straight away from the
 * first caster (the far side of whatever is between), then turning towards it; inner ring
 * first. A point is usable when its ground is within MAX_HEIGHT_DIFF of the center, the
 * caller's filter does not reject it, no caster sees it, the mover sees it (a clear straight
 * line = a short walk; MovePoint paths anyway) and every watcher sees it. At most MAX_CHECKS
 * line of sight checks per search.
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
        float const RING_INNER = 6.0f;
        float const RING_OUTER = 11.0f;
        // Ground of a candidate within this of the center's height (no ledges, no other floor).
        float const MAX_HEIGHT_DIFF = 3.0f;
        uint32 const MAX_CHECKS = 16;

        struct Search
        {
            Position3 center;
            std::vector<Creature const*> casters;   // the point hides from each of them (not empty)
            Unit const* mover = nullptr;            // walks there
            std::vector<Unit const*> watchers;      // must still see the point (healers)
            // Optional: true = skip this candidate (idle creatures nearby, a patrol's path).
            std::function<bool(Position3 const&)> rejects;
        };

        // True and `point` (with its ground height) when a usable point was found.
        bool FindPoint(Map* map, Search const& search, Position3& point);

        // Pure: the creature's profile has a main ranged spell (creature_spells
        // CF_MAIN_RANGED_SPELL): it stands back and casts instead of following its victim.
        bool IsRangedCaster(uint32 entry);
    }
}

#endif
