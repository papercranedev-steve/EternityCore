/*
 * PartyBot Commander layer 2 (situation): who of the team waits where the player left them.
 * Design: .kiro/docs/2026-11-21-companion-talk.md §4 (workspace root).
 *
 * "在这儿等我" (the talk window, one member) or "让大家在这等我" (the party unit menu, the whole
 * team) puts members into TeamPlan::stay with the spot they stood on; "跟我走" takes them out.
 * A staying member does not follow anyone, is not teleported to the leader however far he
 * goes, and walks back to its spot after a fight (the bot AI reads Current). It is the same
 * layer as the guide (Guide.h): where the team is, not what one bot does.
 *
 * Works without a real leader: the spot is the member's own, nothing here reads the leader.
 * A stay ends only when told (Follow), or when the member is no longer of the team (Current
 * stops returning it).
 *
 * Map thread of the party (chat commands run there too, like `.partybot ai guide`).
 */

#ifndef MANGOS_COMMANDER_STAY_H
#define MANGOS_COMMANDER_STAY_H

#include "Common.h"

class Player;

namespace Commander
{
    struct StaySpot;
    struct TeamPlan;

    namespace Stay
    {
        // `member` waits where it stands now (again: the spot moves there). False: not in world.
        bool Begin(TeamPlan& plan, Player const* member);
        // `member` follows again. False: it was not staying.
        bool End(TeamPlan& plan, Player const* member);
        // Its spot while it stays (on its own map), else null.
        StaySpot const* Current(TeamPlan const& plan, Player const* member);
    }
}

#endif
