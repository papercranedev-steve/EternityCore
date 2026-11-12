/*
 * PartyBot Commander layer 1 (eyes): where a jump forward (mage Blink: the server moves the
 * caster `distance` yards the way it faces) would land, for the directions away from `awayFrom`
 * (the enemies running at it), up to 90 degrees to either side, one per position field cell on
 * the jump's circle, straight away first (doc .kiro/docs/2026-11-12-mage-hold.md 3.4: away from
 * the chasers, a safe landing; user 2026-11-12: use the position field).
 * A landing is kept when
 *   - its position field cell is standable (ground, linked to the party), no idle creature
 *     covers it and no patrol comes there before the fight ends (Value::FightRemainingSec);
 *     outside the field: Map::GetHeight, SafeRoute::IdleCreatures and PatrolZone instead;
 *   - the ground is within MAX_STEP_Z of the bot, out of ranged hazard zones;
 *   - the bot sees it: the jump is a straight line, the field's links are walks.
 * Live map reads (sight): map thread.
 */

#ifndef MANGOS_COMMANDER_LEAP_SPOTS_H
#define MANGOS_COMMANDER_LEAP_SPOTS_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Player;

namespace Commander
{
namespace LeapSpots
{
    static constexpr float MAX_STEP_Z = 4.0f;
    static constexpr float EYE_HEIGHT = 2.0f;

    struct Spot
    {
        float orientation = 0.0f;   // face this way, then jump
        Position3 landing;
    };

    std::vector<Spot> Find(Player* me, WorldModel const& world, float distance, Position3 const& awayFrom);
}
}

#endif
