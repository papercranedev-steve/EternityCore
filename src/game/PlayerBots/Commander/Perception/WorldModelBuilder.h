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
    // Radius around the party leader scanned for patrols (WorldModel::patrols), same scan.
    float const PATROL_SCAN_RADIUS = 100.0f;
    // Creature spell lists are checked this often (CreatureAI.cpp CREATURE_CASTING_DELAY).
    float const CREATURE_SPELL_CHECK_SEC = 1.2f;

    // Allies (WorldModel::allies): friendly non-party creatures in combat within this range of
    // the leader, at most MAX_ALLIES (nearest first). Enemies fighting them only are added up
    // to MAX_ALLY_ENEMIES (a town guard fight must not flood the enemy list).
    float const ALLY_RANGE = 40.0f;
    size_t const MAX_ALLIES = 10;
    size_t const MAX_ALLY_ENEMIES = 20;
    // AllyFacts::tankLike: elite and max health >= this x the party tank's max health.
    float const ALLY_TANK_HEALTH_MULT = 2.0f;
    // Interrupts with a longer range (InterruptAbility::maxRange, at least 5) count as ranged:
    // EnemyFacts::outOfSight is checked for bots that have one.
    float const MELEE_INTERRUPT_RANGE = 5.0f;

    // Party members, pets, allies, enemies and their threat lists. No grid search: allies come
    // from the last scan (allyCandidates), the previous allies, and the threat lists of enemies.
    void RefreshCombat(PartyBotAI* bot, WorldModel& world);

    // Out-of-combat creatures near the leader, grouped into packs, and the ally candidate scan.
    // Grid searches, run less often.
    void RefreshPacks(PartyBotAI* bot, WorldModel& world);
}
}

#endif
