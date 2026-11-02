/*
 * How the tank opens a pull, chosen by the player (`.partybot ai pullmethod`, the addon's
 * pull method button). PullPlanner::Plan reads the leader's choice for `pull` and `clear`.
 *
 *   Auto    the planner decides: walk in, or open from range when the target stands back and
 *           casts, walking in would bring more by aggro range, a creature stands next to it,
 *           or the pack is brought back.
 *   Ranged  always open from range when the tank has a ranged pull (FindRangedPullSpell);
 *           without one it walks in (reported).
 *   Melee   always walk in (Charge when the pack is fought where it stands).
 */

#ifndef MANGOS_COMMANDER_PULL_METHOD_H
#define MANGOS_COMMANDER_PULL_METHOD_H

#include "Common.h"
#include <cstring>

namespace Commander
{
    enum class PullMethod : uint8
    {
        Auto = 0,
        Ranged,
        Melee,
    };

    inline char const* PullMethodName(PullMethod method)
    {
        switch (method)
        {
            case PullMethod::Ranged: return "ranged";
            case PullMethod::Melee:  return "melee";
            default:                 return "auto";
        }
    }

    // False for an unknown name.
    inline bool ParsePullMethod(char const* name, PullMethod& out)
    {
        if (!name)
            return false;
        if (!strcmp(name, "auto"))
            out = PullMethod::Auto;
        else if (!strcmp(name, "ranged"))
            out = PullMethod::Ranged;
        else if (!strcmp(name, "melee"))
            out = PullMethod::Melee;
        else
            return false;
        return true;
    }
}

#endif
