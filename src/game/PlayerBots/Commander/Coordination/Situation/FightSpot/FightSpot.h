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
 *   walk      (S not the tank's place) Value::FightExtension(walk to S at WALK_YARDS_PER_SEC,
 *             the tank's backwards walk with the pile on it (Backpedal.h),
 *             the grid walk or the straight line x PATH_FACTOR);
 *   area      (tank fight spot doc 3.4, user 2026-11-14: the tank keeps the pile where the
 *             mage's worst-case Blizzard wants it, the far half of the circle, not its centre;
 *             the same valuation as the mage's, no zone of its own)
 *             in the fight, a bot's area channel running: its AreaValue for the ticks left now
 *             - with the enemies on the tank around S (each keeps its place relative to the
 *             tank) and the tank busy walking (MAX_VALUED best cells by the other terms only;
 *             replaces the area phase's end cost). Between casts 0: the next circle is put on
 *             the pile wherever it is (AreaCentre::NearSide).
 *             before the fight (Request::walkFromAt), the planned area caster with a ground
 *             channel (AreaIntent::PlannedCaster): its best cell for a pile at S by AreaSpot's
 *             pre-fight score (slip + walk, AreaSpot::CasterCells / SlipAt, within range -
 *             SPOT_MARGIN_YD; none: as if it cast standing on the pile) - the same at the
 *             pull's spot. Negative: more room at S.
 * The tank's place costs patrols + holds. In the fight at most MARGIN_SEC of the tank's
 * LostAction: nothing to gain, no search (the area term is only a loss of moving there). Else
 * the field cells MIN_YD..MAX_YD around the place (FieldSpots::Collect: standable, no idle
 * creature, reachable) are scored; the best MAX_SPOTS that lose less than the tank's place by
 * more than the margin are the move's spots, best first (ground, idle creatures and the path
 * are checked live by TankStanceBehavior on the tank). Called every refresh by
 * FightSpotAssigner (tank fight spot doc .kiro/docs/2026-11-14-tank-fight-spot.md).
 * Not scored yet (doc 3.3): hazards (HazardZones mark no zone dangerous to tanks), fleeing
 * enemies' helpers, how well the healers and ranged can stand around S (they pick their own
 * cells around the tank afterwards, Healer / RangedStanceBehavior).
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
    // Backwards run speed (4.5 yd/s, measured 2026-11-08); was the forward 7 until 2026-11-14.
    float const WALK_YARDS_PER_SEC = 4.5f;
    float const PATH_FACTOR = 1.3f;
    size_t const MAX_SPOTS = 8;
    // The running channel is valued (AreaValue, a world copy) for the best this many by the
    // other terms only (doc 9, answer 5).
    size_t const MAX_VALUED = 8;

    struct Choice
    {
        float hereLoss = 0.0f;          // patrols + holds where the tank is
        float herePatrols = 0.0f;
        float hereHolds = 0.0f;
        PatrolFacts const* patrol = nullptr;    // the patrol costing the most here (null = none)
        std::vector<Position3> spots;   // better spots, best first; empty = stay
        float bestLoss = 0.0f;          // the first spot's patrols + holds + area
        float bestArea = 0.0f;          // of which the area term (negative: a gain over here)
        float bestWalk = 0.0f;          // the first spot's walk
        std::string why;
    };

    // Where the pile is (or will be) and for how long.
    struct Request
    {
        Position3 at;                   // the pile: the tank in the fight, the pull's spot before it
        float remainingSec = 0.0f;      // the fight from now (Value::FightRemainingSec, the pull's estimate before)
        float patrolWithinSec = 0.0f;   // patrols arriving later are not counted (judged again then)
        // Before the fight: the tank walks from `at` (it opens there, then takes the pile to the
        // spot), straight line x PATH_FACTOR. In the fight: the grid walk from the tank.
        bool walkFromAt = false;
    };

    // In the fight: the tank's place, the fight's remaining time.
    Request InFight(WorldModel const& world, PartyMemberFacts const& tank, float patrolWithinSec);

    Choice Choose(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank, Request const& r);
}
}

#endif
