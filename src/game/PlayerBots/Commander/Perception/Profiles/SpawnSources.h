/*
 * PartyBot Commander layer 1 (eyes): which creature entries the world brings by a trap, a
 * death or an aggro (doctrine todo 9, .kiro/docs/2026-11-09-doctrine/2026-11-10-roles-sources.md).
 * Static data, built once on first use (any thread):
 *   Trap   summoned by the spell of a trap game object (GAMEOBJECT_TYPE_TRAP, trap.spellId):
 *          Stratholme's Supply Crates cast Bugs, 2-6 Plagued Insects.
 *   Death  in a creature's unconditional EventAI death event (CreatureProfile::deathSpawns):
 *          Broken Cadaver from a Ravaged Cadaver.
 *   Aggro  in an unconditional EventAI aggro event (CreatureProfile::aggroSpawns): Rockwing
 *          Gargoyles from the Eye of Naxxramas.
 * An entry can be several (bit mask).
 */

#ifndef MANGOS_COMMANDER_SPAWN_SOURCES_H
#define MANGOS_COMMANDER_SPAWN_SOURCES_H

#include "Common.h"

namespace Commander
{
    namespace SpawnSources
    {
        enum : uint8
        {
            FROM_TRAP  = 1 << 0,
            FROM_DEATH = 1 << 1,
            FROM_AGGRO = 1 << 2,
        };

        uint8 Of(uint32 entry);
    }
}

#endif
