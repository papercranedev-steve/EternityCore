/*
 * PartyBot Commander: who plays together (.kiro/steering/ai-architecture.md, "one Commander for
 * teams of any size"). A team is
 *  - a WoW group (a real player's party, the party bots in it), or
 *  - a unit of bots the population system put together (Population/), without a WoW group:
 *    a field bot alone is a team of one.
 * The Commander has one PartyCommander per team and map instance, and takes the members from
 * here, never from Player::GetGroup directly.
 *
 * Threads: units are joined / left from the world thread (maps idle) or the member's own map
 * thread; readers are map threads. A mutex guards the unit table.
 */

#ifndef MANGOS_COMMANDER_TEAM_H
#define MANGOS_COMMANDER_TEAM_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Player;

namespace Commander
{
    namespace Team
    {
        // 0 = no team. A group: its id; a unit: UNIT_KEY_BIT | unit id.
        uint64 const UNIT_KEY_BIT = uint64(1) << 32;

        // A bot joins the population unit `unitId` (!= 0); the first one in leads it. A bot in
        // a WoW group is in that group's team whatever its unit.
        void JoinUnit(ObjectGuid member, uint32 unitId);
        void LeaveUnit(ObjectGuid member);

        uint64 KeyOf(Player const* member);
        bool HasTeam(Player const* member);
        bool SameTeam(Player const* a, Player const* b);
        // Everyone of `member`'s team that is online, `member` included (any map: check the map
        // before touching another member, other maps belong to other threads). Empty without a team.
        std::vector<Player*> Members(Player const* member);
        // The group's leader, or the unit's first member.
        ObjectGuid LeaderOf(Player const* member);
        // More than one member (online or not).
        bool IsShared(Player const* member);
    }
}

#endif
