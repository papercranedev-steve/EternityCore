/*
 * PartyBot Commander layer 1 (eyes): where a jump forward (mage Blink: the server moves the
 * caster `distance` yards the way it faces) would land, for a few directions away from
 * `awayFrom` (the enemies running at it): straight away and 30 / 60 / 90 degrees to either side
 * (doc .kiro/docs/2026-11-12-mage-hold.md 3.4: away from the chasers, a safe landing). A
 * landing is kept when the ground there is within MAX_STEP_Z of the bot, the bot sees it
 * (nothing in between to stop the jump short), and it is out of idle creatures' reach
 * (SafeRoute::IdleCreatures), ranged hazard zones and the way of a patrol (PatrolZone).
 * Live map reads: map thread.
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
