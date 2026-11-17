/*
 * PartyBot Commander layer 2 (situation): who the party follows out of combat.
 *
 * Normally the player. A party bot can be made the guide (TeamPlan::guide, the planned push
 * mode: the tank walks on through the dungeon and the others follow it). The guide itself
 * follows nobody: it stands until something walks it (`.partybot cometome`, later the push
 * planner). Everyone else follows it with the same follow as the player's (FollowSpot,
 * HumanFollow, the teleport beyond 100 yd).
 *
 * Fallback (the guide may fail): a guide that is dead, gone, on another map or out of the
 * party does not lead; the party follows the player again until it is back. The guide itself
 * falls back to the player's follow then too.
 *
 * Not yet for a bot guide: LeaderTrail and WayIn are recorded from client movement packets, a
 * bot sends none, so TrailFollow (the leader's way round a drop) and the retreat's way in are
 * still the player's.
 *
 * Map thread of the party.
 */

#ifndef MANGOS_COMMANDER_GUIDE_H
#define MANGOS_COMMANDER_GUIDE_H

#include "Common.h"

class Player;

namespace Commander
{
    struct TeamPlan;

    namespace Guide
    {
        // The guide of `member`'s party when it can lead now, else null (follow the player).
        Player* Current(TeamPlan const& plan, Player const* member);
    }
}

#endif
