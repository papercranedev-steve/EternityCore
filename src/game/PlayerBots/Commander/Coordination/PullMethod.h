/*
 * How the tank opens a pull, chosen by the player (`.partybot ai pullmethod`, the addon's
 * pull method button). PullPlanner::Plan reads the leader's choice for `pull` and `clear`.
 *
 *   Auto    the planner decides: walk in, or open from a distance when the target stands back
 *           and casts, walking in would bring more by aggro range, a creature stands next to
 *           it, or the pack is brought back. From a distance = a shot or a face pull, whichever
 *           brings fewer (fewer elites; a shot on a tie: more room to the aggro ranges).
 *   Ranged  always shoot when the tank has a ranged pull (FindRangedPullSpell); without one,
 *           or no spot to shoot from, it walks in (reported).
 *   Face    always face pull: stand where the target sees the tank, inside its aggro range,
 *           and let it come (Perception/ShotSpot.h FindFace); no spot: walks in (reported).
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
        Face,
    };

    inline char const* PullMethodName(PullMethod method)
    {
        switch (method)
        {
            case PullMethod::Ranged: return "ranged";
            case PullMethod::Melee:  return "melee";
            case PullMethod::Face:   return "face";
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
        else if (!strcmp(name, "face"))
            out = PullMethod::Face;
        else
            return false;
        return true;
    }
}

#endif
