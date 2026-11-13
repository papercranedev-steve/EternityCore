/*
 * PartyBot Commander layer 2 (brain), situation step: where the main tank keeps the pile in the
 * fight (battlefield groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md 8;
 * user 2026-11-13: on the position field like every other movement, no special geometry; the
 * spot scoring doc .kiro/docs/2026-11-11-spot-scoring.md 3.4 "where the tank takes the pile").
 * Replaces PatrolAssigner's ring spots (15 / 25 / 35 yd x 12) and "benefit > cost" per patrol.
 *
 * Loss of a spot S, in points (Perception/Values/SpotCost.h):
 *   patrols   SpotCost::PatrolJoins(S): the patrols that walk to the pile at S before the fight
 *             ends (only those within `patrolWithinSec`: the pull still places the tank);
 *   holds     SpotCost::BreaksHold(S): the controls that break on damage within
 *             ControlValue::ClearOfPileYd of S (the area spells there would wake them);
 *   walk      (S not the tank's place) Value::FightExtension(walk to S at RUN_YARDS_PER_SEC,
 *             the grid walk or the straight line x PATH_FACTOR) + the area phase's end cost
 *             (plan.area, the last refresh: AreaPhase runs after this).
 * The tank's place costs patrols + holds. At most MARGIN_SEC of the tank's LostAction: nothing
 * to gain, no search. Else the field cells MIN_YD..MAX_YD around the tank (FieldSpots::Collect:
 * standable, no idle creature, reachable) are scored; the best MAX_SPOTS that lose less than the
 * tank's place by more than the margin are the move's spots, best first (ground, idle creatures
 * and the path are checked live by PatrolBehavior on the tank).
 * Not scored yet (doc 8.1): how well the healers and ranged can stand around S (they pick their
 * own cells around the tank afterwards, Healer / RangedStanceBehavior).
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_FIGHT_SPOT_H
#define MANGOS_COMMANDER_FIGHT_SPOT_H

#include "Commander/Coordination/TeamPlan.h"
#include <string>
#include <vector>

namespace Commander
{
namespace FightSpot
{
    float const MIN_YD = 5.0f;
    float const MAX_YD = 35.0f;
    float const MARGIN_SEC = 1.0f;
    float const RUN_YARDS_PER_SEC = 7.0f;
    float const PATH_FACTOR = 1.3f;
    size_t const MAX_SPOTS = 8;

    struct Choice
    {
        float hereLoss = 0.0f;          // patrols + holds where the tank is
        float herePatrols = 0.0f;
        float hereHolds = 0.0f;
        PatrolFacts const* patrol = nullptr;    // the patrol costing the most here (null = none)
        std::vector<Position3> spots;   // better spots, best first; empty = stay
        float bestLoss = 0.0f;          // the first spot's patrols + holds
        float bestWalk = 0.0f;          // the first spot's walk
        std::string why;
    };

    Choice Choose(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank, float patrolWithinSec);
}
}

#endif
