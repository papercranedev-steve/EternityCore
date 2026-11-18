/*
 * PartyBot Commander layer 1: reads live game objects into a WorldModel.
 * The only perception code that touches Player / Creature pointers. Map thread only.
 */

#ifndef MANGOS_COMMANDER_WORLD_MODEL_BUILDER_H
#define MANGOS_COMMANDER_WORLD_MODEL_BUILDER_H

#include "Commander/Perception/WorldModel.h"

class TeamBotAI;
class Player;

namespace Commander
{
struct CreatureProfile;

namespace WorldModelBuilder
{
    // Radius around the party leader scanned for out-of-combat packs; also the largest
    // `.partybot ai clear` radius (the clear only sees packs in this scan).
    float const PACK_SCAN_RADIUS = 80.0f;
    // Radius around the party leader scanned for patrols (WorldModel::patrols), same scan.
    float const PATROL_SCAN_RADIUS = 100.0f;
    // Each patrol's walk is predicted one lap ahead (PatrolTracker::LapSec), at least
    // PATROL_TRACK_MIN_SEC (a path that stops at its end, a spline) and at most
    // PATROL_TRACK_MAX_SEC (a long loop), one point per step.
    float const PATROL_TRACK_MIN_SEC = 60.0f;
    float const PATROL_TRACK_MAX_SEC = 180.0f;
    float const PATROL_TRACK_STEP_SEC = 1.0f;
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
    // Combat range abilities (rangeIndex 2: Taunt, Shield Bash, Kick, Gouge): the server needs
    // the centres within both combat reaches + 4/3 yd (WorldObject::CanReachWithMeleeSpellAttack),
    // about the target's bounding radius + 2.8 yd for a player. The abilities' maxRange is in
    // "centre distance minus the target's bounding radius", so this, not the DBC 5.5 yd: the
    // tank stood 6 yd off a loose enemy, the brain counted it in Taunt range and nobody walked
    // in (strat-20261108-200046).
    float const COMBAT_RANGE = 2.5f;

    // Party members, pets, allies, enemies and their threat lists. No grid search: allies come
    // from the last scan (allyCandidates), the previous allies, and the threat lists of enemies.
    void RefreshCombat(TeamBotAI* bot, WorldModel& world);

    // Out-of-combat creatures near the party, grouped into packs, and the ally candidate scan.
    // Grid searches, run less often. Packs and their walks (PackRoutes) are around `guide` when
    // the party follows one (Guide::Current, null = none), else around the leader.
    void RefreshPacks(TeamBotAI* bot, WorldModel& world, Player* guide);

    // Damage per second of a creature's main ranged spell (CreatureProfile::mainRangedSpellId)
    // while it stands back casting: one cast per (cast time + half a spell list check, or the
    // entry's repeat delay, whichever is longer). 0 = none or unknown. Before resistances.
    float RangedSpellDps(CreatureProfile const& profile, uint32 level, float meleeHit);
}
}

#endif
