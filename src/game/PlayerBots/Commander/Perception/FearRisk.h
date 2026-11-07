/*
 * PartyBot Commander layer 1 (eyes): which idle packs a fear may bring into the fight, and how
 * likely (crowd control doc 2026-11-07 section 2.4). Pure, on WorldModel facts.
 *
 * Server rules (code reading, not tested in game yet):
 *  - FleeingMovementGenerator: a feared creature walks legs in random directions, a pause of
 *    0.8-1.5 s after each; it stays within about MAX_RUN_YD of the one who feared it (quiet
 *    distance 28-43 yd).
 *  - A creature in combat walking past idle friends does not make them join (BasicAI::
 *    MoveInLineOfSight only reacts to hostile units; assistance is called once, when a
 *    creature starts attacking). What brings a pack in is a party member chasing the feared
 *    creature into its aggro range (melee bots chase their victim, pets too).
 *  - An area fear hits every enemy in its radius, idle ones included: those join for sure.
 *
 * Per pack (WorldModel::packs, members that are hostile and may start a fight):
 *   certain   an area fear and a member (any, neutral too) within the radius of the caster
 *   else      f = share of sample points around the caster (rings out to MAX_RUN_YD) within a
 *             member's aggro reach + CHASE_MARGIN_YD; legs = duration / LEG_SEC;
 *             chance = (1 - (1 - f)^legs) x (melee member on a feared enemy ? 1 : NO_CHASER)
 * The whole pack joins when one member does (assistance, aggro-together groups).
 */

#ifndef MANGOS_COMMANDER_FEAR_RISK_H
#define MANGOS_COMMANDER_FEAR_RISK_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
namespace FearRisk
{
    float const MAX_RUN_YD = 43.0f;
    float const LEG_SEC = 2.5f;             // one leg of the run plus the pause after it
    float const CHASE_MARGIN_YD = 2.0f;
    // Nobody chases (only ranged on it, or it was on the healer): a bot may still switch to it
    // and follow, or the tank walk over to pick it up. Untested guess.
    float const NO_CHASER_FACTOR = 0.25f;
    float const MIN_CHANCE = 0.02f;

    struct PackRisk
    {
        PackFacts const* pack = nullptr;
        float chance = 0.0f;
        bool certain = false;               // in the area fear's radius
    };

    // `caster`: where the fear is cast from; `radius` > 0 for an area fear (0 = single target);
    // `feared`: the enemies it fears.
    std::vector<PackRisk> Of(WorldModel const& world, Position3 const& caster, float radius, float durationSec,
                             std::vector<EnemyFacts const*> const& feared);
}
}

#endif
