/*
 * PartyBot Commander layer 3: why the stance modules' spot search took no cell, for their
 * "no spot" trace (2026-11-15, stockade-20261115-000917: mage and priest stood in the
 * corridor for 30 s and the trace only said how many cells were checked).
 *   idle      cells this bot's own idle scan puts in an idle creature's reach
 *   hidden    cells that do not see the target (sight cache)
 *   budget    the tick's sight budget ran out (the next ticks go on)
 *   seen      cells that see the target
 *   live      seen, but the line from the exact positions now does not
 *   path      SafeRoute::PathIsSafe failed (then kept as failed: SightCache::PathFailed)
 *   pathKept  skipped, failed the path a moment ago
 */

#ifndef MANGOS_COMMANDER_SPOT_REJECTS_H
#define MANGOS_COMMANDER_SPOT_REJECTS_H

#include "Commander/Trace/Trace.h"
#include <string>

namespace Commander
{
    struct SpotRejects
    {
        uint32 idle = 0;
        uint32 hidden = 0;
        uint32 seen = 0;
        uint32 live = 0;
        uint32 path = 0;
        uint32 pathKept = 0;
        bool budget = false;

        // JSON fields without the braces.
        std::string Json() const
        {
            return Trace::Format("\"idle\":%u,\"hidden\":%u,\"budget\":%s,\"seen\":%u,\"live\":%u,\"path\":%u,\"pathKept\":%u",
                                 idle, hidden, budget ? "true" : "false", seen, live, path, pathKept);
        }
    };
}

#endif
