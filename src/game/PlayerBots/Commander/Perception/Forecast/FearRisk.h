/*
 * PartyBot Commander layer 1 (eyes): which idle packs a fear may bring into the fight (crowd
 * control doc 2026-11-07 section 2.4). Pure, on WorldModel facts.
 *
 * Server rules (code reading 2026-11-26, strat-20261126-224046: a Turn Undead on a Ravaged
 * Cadaver ran 74 yd down the street and brought three groups, valued "adds 0"):
 *  - Every fear (SPELL_AURA_MOD_FEAR: Fear, Turn Undead, Psychic Scream, Intimidating Shout,
 *    Howl of Terror, Death Coil) runs FleeingMovementGenerator. Legs in random directions,
 *    each a navmesh path of at most 30 yd, a pause of 0.8-1.5 s after each. Leg length by
 *    the distance d to the one who feared it: d < 28: 0.4-1.3 x (28 - d); 28..38: 6-12 yd;
 *    beyond 38: 4-10 yd. Nothing pulls it back: in a street or corridor it keeps going.
 *  - The spells differ in their data only: duration (Death Coil 3 s, Psychic Scream and
 *    Intimidating Shout 8 s, Howl of Terror 10 / 15 s, Fear and Turn Undead 20 s) and a run
 *    speed bonus (SPELL_AURA_MOD_INCREASE_SPEED 25% on all of them but Death Coil:
 *    ControlAbility::fearSpeedMult).
 *  - Every update while it runs, Creature::CallForHelp(10): idle creatures within 10 yd that
 *    are friendly to it attack its victim, and bring their groups. (Until 2026-11-26 this
 *    header said a runner does not make them join.) After the fear, a creature in combat more
 *    than 10 yd from its spawn calls for help every second on its way back (Creature::Update,
 *    m_callForHelpDist): not modelled, the run's reach already counts the way out.
 *  - An area fear hits every enemy in its radius, idle ones included: those join for sure.
 *
 * Worst case (as FleePredictor, user 2026-11-08: every creature the run can come near joins):
 *   sec      what the control holds (ControlValue::ExpectedSec: 0 for a fear that breaks on
 *            damage while a party DoT ticks on it)
 *   reach    RunYd: the legs of the server's rules at their longest (12 / 10 yd, the first
 *            one up to 30) plus the random point around the leg's end (5 yd, within the 30 yd
 *            path), straight on away from the caster, at its speed x fearSpeedMult,
 *            the shortest pause (0.8 s) after each, until `sec`
 *   joins    a pack with a member that may answer (not noAggro, not tapped by another; any
 *            faction: the pure facts do not have it) within reach + CALL_YD of a feared enemy
 *            (straight line: walls are not seen), or within reach + its aggro reach +
 *            CHASE_MARGIN_YD when a melee member is on that enemy and chases it
 *   certain  an area fear and a member (any, neutral too) within the radius of the caster
 * The whole pack joins when one member does (assistance, aggro-together groups).
 * Known limits: straight lines (a wall between run and pack still counts it); the pack scan
 * (WorldModelBuilder::PACK_SCAN_RADIUS around the leader) bounds what is seen.
 */

#ifndef MANGOS_COMMANDER_FEAR_RISK_H
#define MANGOS_COMMANDER_FEAR_RISK_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
namespace FearRisk
{
    // FleeingMovementGenerator (MIN_QUIET_DISTANCE, MAX_QUIET_DISTANCE - SEARCH_RANDOM_RADIUS).
    float const QUIET_NEAR_YD = 28.0f;
    float const QUIET_FAR_YD = 38.0f;
    float const NEAR_LEG_FACTOR = 1.3f;     // d < 28: up to 1.3 x (28 - d)
    float const QUIET_LEG_YD = 12.0f;       // 28..38: up to 1.2 x 10
    float const FAR_LEG_YD = 10.0f;         // beyond 38: up to 1.0 x 10
    float const RANDOM_POINT_YD = 5.0f;     // GetWalkRandomPosition around the leg's end (SEARCH_RANDOM_RADIUS)
    float const LEG_PATH_YD = 30.0f;        // PathFinder::setPathLengthLimit(30)
    float const MIN_PAUSE_SEC = 0.8f;
    float const CALL_YD = 10.0f;            // CallForHelp(10)
    float const CHASE_MARGIN_YD = 2.0f;

    struct PackRisk
    {
        PackFacts const* pack = nullptr;
        float chance = 0.0f;                // 1: the worst case counts every reached pack
        bool certain = false;               // in the area fear's radius
    };

    // Yards a fear of `sec` seconds runs at most (header), starting `fromCasterYd` from the
    // one who feared it, at `speed` yards per second (the bonus included).
    float RunYd(float sec, float speed, float fromCasterYd);

    // `caster`: where the fear is cast from; `radius` > 0 for an area fear (0 = single target);
    // `sec`: what it holds; `speedMult`: ControlAbility::fearSpeedMult; `feared`: the enemies it fears.
    std::vector<PackRisk> Of(WorldModel const& world, Position3 const& caster, float radius, float sec,
                             float speedMult, std::vector<EnemyFacts const*> const& feared);
}
}

#endif
