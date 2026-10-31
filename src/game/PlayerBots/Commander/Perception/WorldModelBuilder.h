/*
 * PartyBot Commander layer 1: reads live game objects into a WorldModel.
 * The only perception code that touches Player / Creature pointers. Map thread only.
 */

#ifndef MANGOS_COMMANDER_WORLD_MODEL_BUILDER_H
#define MANGOS_COMMANDER_WORLD_MODEL_BUILDER_H

#include "Commander/Perception/WorldModel.h"

class PartyBotAI;

namespace Commander
{
namespace WorldModelBuilder
{
    // Radius around the party leader scanned for out-of-combat packs.
    float const PACK_SCAN_RADIUS = 50.0f;

    // Party members, pets, enemies and their threat lists. Cheap (no grid search).
    void RefreshCombat(PartyBotAI* bot, WorldModel& world);

    // Out-of-combat creatures near the leader, grouped into packs. Grid search, run less often.
    void RefreshPacks(PartyBotAI* bot, WorldModel& world);
}
}

#endif
