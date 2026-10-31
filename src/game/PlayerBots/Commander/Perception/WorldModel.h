/*
 * PartyBot Commander layer 1 (perception): facts about one party on one map.
 * Filled by Perception/, read-only for every other layer. Map thread only.
 */

#ifndef MANGOS_COMMANDER_WORLD_MODEL_H
#define MANGOS_COMMANDER_WORLD_MODEL_H

#include "Common.h"

namespace Commander
{
    struct WorldModel
    {
        uint32 version = 0;          // +1 on every refresh
        uint32 refreshedAtMs = 0;
        // Step 2 adds: enemies, ThreatView, packs.
    };
}

#endif
