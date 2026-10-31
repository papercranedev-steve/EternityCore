/*
 * PartyBot Commander layer 2 (team assignment): the party plan.
 * Plain data written by assigners, read by behaviors. Map thread only.
 */

#ifndef MANGOS_COMMANDER_TEAM_PLAN_H
#define MANGOS_COMMANDER_TEAM_PLAN_H

#include "Common.h"

namespace Commander
{
    struct TeamPlan
    {
        uint32 version = 0;
        // Step 3 adds: threat duties. Later: focus target, interrupts, crowd control, pull plan.
    };
}

#endif
