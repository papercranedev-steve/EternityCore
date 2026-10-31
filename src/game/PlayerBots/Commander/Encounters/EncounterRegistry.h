/*
 * PartyBot Commander: explicit list of boss modules (code-structure doc 6.1: self-registering
 * statics are dropped by the linker in the static game library).
 * One line per module in EncounterRegistry.cpp: map id + factory.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_REGISTRY_H
#define MANGOS_COMMANDER_ENCOUNTER_REGISTRY_H

#include "Commander/Encounters/IEncounter.h"

namespace Commander
{
    namespace EncounterRegistry
    {
        // New instances of every module for this map (fresh state, owned by one PartyCommander).
        void Create(uint32 mapId, EncounterList& out);

        // True if any module is listed for this map. Lets EncounterBehavior skip the commander
        // lookup everywhere else.
        bool HasEncounterOnMap(uint32 mapId);
    }
}

#endif
