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
 *             tank) and the tank busy walking (MAX_VALUED best cells by the other terms only,
 *             and the MAX_VALUED best of them inside the circle; replaces the area phase's end
 *             cost). Both ways (2026-11-14, strat-20261114-172715): negative when more of them
 *             are in the circle at S, and some of the tank's enemies outside the circle make
 *             the tank search although nothing else is lost here (the tank takes the pile
 *             into the Blizzard). Between casts 0: the next circle is put on the pile
 *             wherever it is (AreaCentre::NearSide), and not started while the tank moves the
 *             fight (AreaPhase).
 *             before the fight (Request::walkFromAt), the planned area caster with a ground
 *             channel (AreaIntent::PlannedCaster): its best cell for a pile at S by AreaSpot's
 *             pre-fight score (slip + walk, AreaSpot::CasterCells / SlipAt, within range -
 *             SPOT_MARGIN_YD; none: as if it cast standing on the pile) - the same at the
 *             pull's spot. Negative: more room at S.
 * The tank's place costs patrols + holds + flee. In the fight at most MARGIN_SEC of the tank's
 * LostAction and no enemy of the tank outside a channel's circle: nothing to gain, no search
 * (the team term only prices the cells). Else
 * the field cells MIN_YD..MAX_YD around the place (FieldSpots::Collect: standable, no idle
 * creature, reachable) are scored; the best MAX_SPOTS that lose less than the tank's place by
 * more than the margin are the move's spots, best first (ground, idle creatures and the path
 * are checked live by TankStanceBehavior on the tank). Called every refresh by
 * FightSpotAssigner (tank fight spot doc .kiro/docs/2026-11-14-tank-fight-spot.md).
 *   flee      (doc 3.5, user 2026-11-14: fight away from the packs a runner would bring) the
 *             creatures on the tank that run for help (EnemyFacts::flee predicted, not spent or
 *             prevented; before the fight the pull's, WorldModel::pullRunners): the joiners
 *             whose caller is within the run with the party on it (focusedReachYd) + CALL_YD of
 *             its start at S (straight line, FleePredictor::Reached), points as PointsWithin
 *             (FleePredictor::PointsOf: the fight now, before it the pull's creatures from the
 *             packs, with the joiners arriving when it runs), cached by the joiners reached.
 *             Counted at the pile's place too (it can make the tank move).
 *   team      (doc 3.6) every healer and ranged damage dealer bot that can act (not the tank,
 *             not the area phase's caster): its best cell around S by its stance module's
 *             terms (Perception/Values/StandLoss.h; the enemies on the tank moved with the pile
 *             in the fight), no cell: LostAction(StandLoss::HORIZON_SEC); minus the same at the
 *             pile's place. Negative: they stand better at S. MAX_VALUED best cells only.
 * Before the fight the cells are always searched (once per pull): the room and team terms
 * can make another cell better although nothing is lost here.
 * Not scored yet (doc 3.3): hazards (HazardZones mark no zone dangerous to tanks).
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
        float hereLoss = 0.0f;          // patrols + holds + flee at the pile's place
        float herePatrols = 0.0f;
        float hereHolds = 0.0f;
        float hereFlee = 0.0f;
        PatrolFacts const* patrol = nullptr;    // the patrol costing the most here (null = none)
        std::vector<Position3> spots;   // better spots, best first; empty = stay
        float bestLoss = 0.0f;          // the first spot's patrols + holds + area
        float bestArea = 0.0f;          // of which the area term (negative: a gain over here)
        bool channel = false;           // the area term is a channel running (else the caster's room before the fight)
        float bestTeam = 0.0f;          // and the party's places (negative: they stand better there)
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
