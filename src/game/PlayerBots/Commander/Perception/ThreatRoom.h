/*
 * PartyBot Commander layer 1 (eyes): how much threat room a damage dealer (or pet) needs on an
 * enemy someone else holds. Facts and thresholds only; who stops or switches is decided by
 * ThreatBehavior (stop hitting) and TargetAssigner (which enemy to hit).
 * Design: .kiro/docs/2026-11-01-threat-behavior.md section 2, target assignment
 * .kiro/docs/2026-11-07/2026-11-07-target-assignment.md 2.1.
 *
 * Stop rule (keep hitting the current target while room >= Stop):
 *   max(STOP_SHARE x holder's threat, own threat rate x cover)
 *   cover = one bot tick, or NEW_ENEMY_LEAD_SEC (at least one tick) on an enemy first seen less
 *   than NEW_ENEMY_SETTLE_SEC ago: a fresh add the tank has 0 (or 20) threat on is pulled by
 *   the first hit (combat scenario 6, VanCleef's guards).
 * Enter rule (switch onto another enemy only while room >= Enter): the same with more margin,
 *   so a bot near the stop line does not switch on, get stopped, switch off and back again:
 *   max(ENTER_SHARE x holder's threat, own rate x max(ENTER_TICKS ticks, cover + one tick)).
 * Own threat rate: measured on that enemy, else the best measured on any enemy (just switched),
 * else Value::FALLBACK_DPS_PER_LEVEL x level (fight just started).
 */

#ifndef MANGOS_COMMANDER_THREAT_ROOM_H
#define MANGOS_COMMANDER_THREAT_ROOM_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace ThreatRoom
    {
        static constexpr float STOP_SHARE = 0.10f;
        static constexpr float ENTER_SHARE = 0.15f;
        static constexpr float ENTER_TICKS = 2.0f;
        static constexpr float NEW_ENEMY_SETTLE_SEC = 3.0f;
        static constexpr float NEW_ENEMY_LEAD_SEC = 2.0f;
        static_assert(ENTER_SHARE > STOP_SHARE, "entering needs more room than the stop rule");

        // Threat `who` adds per second to `enemy` (see file comment).
        float OwnRate(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy);
        // First seen less than NEW_ENEMY_SETTLE_SEC ago.
        bool Settling(WorldModel const& world, EnemyFacts const& enemy);
        // Room `who` must keep to go on hitting `enemy`; `tickSec` = bot update interval.
        float Stop(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy, float tickSec);
        // Room `who` needs to start hitting `enemy`.
        float Enter(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy, float tickSec);
    }
}

#endif
