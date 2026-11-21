/*
 * PartyBot Commander layer 1 (perception), spatial: who of the team is left behind, far from
 * where the team is (another map, or farther than a distance), e.g. a bot that logged in where
 * it logged out. Facts only: who decides to summon them is GatherPlanner.
 *
 * Members on other maps belong to other threads: only read (position, map, alive, combat).
 * Any team size (a 40 man raid: every member of the raid).
 */

#ifndef MANGOS_COMMANDER_LEFT_BEHIND_H
#define MANGOS_COMMANDER_LEFT_BEHIND_H

#include "Common.h"
#include <vector>

class Player;

namespace Commander
{
    struct LeftBehindMember
    {
        Player* member = nullptr;
        bool otherMap = false;
        float distance = 0.0f;      // on the same map
        uint8 rank = 2;             // who the team misses most: 0 the tank, 1 a healer, 2 others
    };

    namespace LeftBehind
    {
        // Bots of `anchor`'s team left behind: alive, out of combat, not flying, not told to wait
        // (Situation/Stay.h), on another map or farther than `farYd` from `anchor`. Players are
        // left out (the player gets his own question). Most missed first, then other maps, then
        // the farthest.
        std::vector<LeftBehindMember> Of(Player* anchor, float farYd);
    }
}

#endif
