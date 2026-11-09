/*
 * Where the party fights a pull, chosen by the player (`.partybot ai pullplace`, the addon's
 * fight place button). Independent of how the tank opens it (PullMethod.h).
 *
 *   Auto    the planner decides: where the pack stands, or brought back to the leader
 *           (PullOdds: add risk), or at a corner when the fight estimate is hard
 *           (PullPlanner::CORNER_*: FightLoad says the tank drops low or the healers run dry).
 *   Corner  always at a corner (for testing the line of sight logic): once the fight started
 *           the tank steps out of sight of the pulled enemies standing away from it, they walk
 *           round the corner to it (PullPlanner::SearchLosBreak). No corner found: fought in place.
 */

#ifndef MANGOS_COMMANDER_PULL_PLACE_H
#define MANGOS_COMMANDER_PULL_PLACE_H

#include "Common.h"
#include <cstring>

namespace Commander
{
    enum class PullPlace : uint8
    {
        Auto = 0,
        Corner,
    };

    inline char const* PullPlaceName(PullPlace place)
    {
        return place == PullPlace::Corner ? "corner" : "auto";
    }

    // False for an unknown name.
    inline bool ParsePullPlace(char const* name, PullPlace& out)
    {
        if (!name)
            return false;
        if (!strcmp(name, "auto"))
            out = PullPlace::Auto;
        else if (!strcmp(name, "corner"))
            out = PullPlace::Corner;
        else
            return false;
        return true;
    }
}

#endif
