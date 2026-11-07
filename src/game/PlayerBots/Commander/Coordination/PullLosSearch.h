/*
 * PartyBot Commander layer 2 (brain): line of sight pull and corner fights. The point search is
 * PullPlanner::SearchLosBreak (map thread, declared and described in PullPlanner.h, defined in
 * PullLosSearch.cpp); when the break ends is LosBreakOver below (pure, PullAssigner).
 * Split out of PullPlanner.cpp (todo item 14), behaviour unchanged.
 */

#ifndef MANGOS_COMMANDER_PULL_LOS_SEARCH_H
#define MANGOS_COMMANDER_PULL_LOS_SEARCH_H

#include "Commander/Coordination/TeamPlan.h"

namespace Commander
{
    namespace PullDetail
    {
        // The break ends when every caster came within PullAssigner::LOS_CASTER_ARRIVED of the
        // tank or cannot come (dead, out of the fight, crowd controlled), turned to someone
        // else, the fight is over, or after PullAssigner::LOS_BREAK_TIMEOUT_MS (`timedOut`).
        bool LosBreakOver(WorldModel const& world, PullPlan const& pull, Position3 const& tankAt, uint32 nowMs,
                          bool& timedOut);
    }
}

#endif
