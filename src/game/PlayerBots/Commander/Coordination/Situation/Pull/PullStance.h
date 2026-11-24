/*
 * Whether the party starts fights on its own, chosen by the player (the talk window's 战斗策略
 * page, `.partybot ai pullstance`), saved per character (CommanderMgr::SetPullStance). Read by AutoClear (Situation/Pull/AutoClear.h).
 *
 *   Active   (default) the tank pulls nearby packs one after another, like `.partybot ai clear`
 *            around the player, whenever the party is idle. Only with a tank bot: without one
 *            the party behaves as Passive.
 *   Passive  the party fights what the player (or an enemy) starts, as before.
 */

#ifndef MANGOS_COMMANDER_PULL_STANCE_H
#define MANGOS_COMMANDER_PULL_STANCE_H

#include "Common.h"
#include <cstring>

namespace Commander
{
    enum class PullStance : uint8
    {
        Active = 0,
        Passive,
    };

    inline char const* PullStanceName(PullStance stance)
    {
        return stance == PullStance::Passive ? "passive" : "active";
    }

    // False for an unknown name.
    inline bool ParsePullStance(char const* name, PullStance& out)
    {
        if (!name)
            return false;
        if (!strcmp(name, "active"))
            out = PullStance::Active;
        else if (!strcmp(name, "passive"))
            out = PullStance::Passive;
        else
            return false;
        return true;
    }
}

#endif
