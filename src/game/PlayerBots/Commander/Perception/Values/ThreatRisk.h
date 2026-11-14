/*
 * PartyBot Commander layer 1 (eyes): how much an enemy on the tank risks being taken off it
 * (.kiro/docs/2026-11-13-tank-threat-priority.md 3, user 2026-11-13). The tank's target
 * (TankTargets) and its area threat (ThreatBehavior) both value threat by what it takes off
 * this risk.
 *
 * For every rival m of the tank on `e` (ThreatRunway::Rivals: on its list, a healer bot's next
 * heal, the area spell the doctrine counts on):
 *   T       min(horizonSec, e's time to die)
 *   p(m)    clamp(1 - m's runway / T, 0, 1): m takes it before it dies
 *   loss(m) LooseEnemy::Loss of e on m instead of the tank for RECLAIM_SEC, or less: only
 *           while e lives after m took it (time to die - m's runway)
 *   risk    max over m of p(m) x loss(m)
 * Every rival that can catch up counts, not only the runner-up now (it changes as the damage
 * dealers start) and not the frailest member of the party (one that never touches `e` would
 * pull the tank onto enemies nobody threatens). Pets are left out (their owner's pet taking
 * an enemy is not a loss the tank fixes). Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_THREAT_RISK_H
#define MANGOS_COMMANDER_THREAT_RISK_H

#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Forecast/ThreatRunway.h"
#include <vector>

namespace Commander
{
    namespace ThreatRisk
    {
        // Seconds an enemy taken off the tank is counted for: until the tank has it back.
        static constexpr float RECLAIM_SEC = 2.0f;

        // An enemy whose risk the tank looks after: on `tank`, acting, not controlled, not a
        // self-destruct (dies anyway) or a trap swarm (the area spells').
        bool Counts(EnemyFacts const& e, ObjectGuid tank);

        // Points (see the file comment); `addTank` = threat the tank puts on `e` first.
        float Of(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
                 std::vector<PlannedThreat> const& planned, float horizonSec, float addTank = 0.0f);
    }
}

#endif
