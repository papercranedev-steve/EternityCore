/*
 * PartyBot Commander layer 2 helper, map thread: where a healer bot hides during a line of
 * sight pull (PullPlan::losHiders, PullPlanner::SearchLosBreak).
 *
 * The tank hides so the casters walk to it. A healer left in their sight is what they turn to
 * after its first heal (strat-20261107-143623: both Skeletal Guardians turned to the priest
 * and kept casting at it from where they stood). The healer hides too, behind the tank as
 * seen from the casters: a caster that turns to it walks past the tank on the way.
 *
 * LosBreak::FindPoint around the tank's point (first ray straight away from the first caster,
 * i.e. behind the tank), with:
 *   hidden from every caster (FindPoint itself);
 *   in sight of the tank's point (heals reach it), MIN_FROM_TANK .. HEAL_RANGE from it
 *   (LosBreak::MAX_DISTANCE caps it at 18 yd);
 *   at least MIN_BEHIND further from the first caster than the tank's point;
 *   the healer gets there by a safe walk (SafeRoute::PathIsSafe, as for the tank);
 *   the caller's `rejects` (idle creatures, a patrol's path);
 *   each caster's own path to the point passes within PATH_PASS of the tank's point
 *   (PathInfo from the caster; no navmesh path: the straight line is used).
 * None found: the healer stays where it is (HealerStanceBehavior moves it if it loses the tank).
 */

#ifndef MANGOS_COMMANDER_LOS_HEALER_SPOT_H
#define MANGOS_COMMANDER_LOS_HEALER_SPOT_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"
#include <functional>
#include <vector>

class Creature;
class Map;
class Player;

namespace Commander
{
    namespace LosHealerSpot
    {
        float const HEAL_RANGE = 36.0f;     // 40 yd heals, some margin for the walk ending short
        // Off the pack that stands around the tank (cleaves, the healer stance's enemy gap).
        float const MIN_FROM_TANK = 8.0f;
        float const MIN_BEHIND = 2.0f;
        float const PATH_PASS = 6.0f;

        bool Find(Map* map, Player* healer, Position3 const& tankPoint, std::vector<Creature*> const& casters,
                  std::function<bool(Position3 const&)> const& rejects, Position3& point);
    }
}

#endif
