/*
 * PartyBot Commander layer 1 (eyes): what it costs a party member to turn to another enemy.
 * Design: .kiro/docs/2026-11-03-discussion/2026-11-03-architecture-refinement.md section 2.
 * A fact ("if X switches to that enemy it loses this much"), not a choice: the brain picks who
 * goes (intercepts, later crowd control and rescues), and the cerebellum may use it too.
 *
 * Seconds of the member's output lost (Value::LostAction, points):
 *   SWITCH_SEC                       retargeting, a swing / cast restarting
 * + castRemainingMs                  the cast or channel it is busy with (waits it out or drops it)
 * + walk                             (walk - reach) / CastValues::RUN_YARDS_PER_SEC; walk = the
 *                                    navmesh path of a melee or tank bot when checked
 *                                    (RouteRisk::WalkYd, MeleeRoutes), else the straight distance
 * + comboPoints x COMBO_POINT_SEC    combo points stay on the old target (1.12), rebuilt one per GCD
 * Zero when it already attacks the enemy.
 *
 * Not counted (first version): DoTs keep ticking after a switch, only their refresh is late;
 * a key ability on cooldown (the intercept needs none in particular yet).
 */

#ifndef MANGOS_COMMANDER_SWITCH_COST_H
#define MANGOS_COMMANDER_SWITCH_COST_H

#include "Commander/Perception/Values/ValueModel.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;
    struct EnemyFacts;

    namespace SwitchCost
    {
        static constexpr float SWITCH_SEC = 0.5f;           // same as ThreatBehavior::SWITCH_TARGET_SEC
        static constexpr float COMBO_POINT_SEC = Value::GCD_SEC;

        // Points `member` loses turning to `enemy`; `reach` = yards it can act from (melee
        // reach, spell range).
        float Of(WorldModel const& world, PartyMemberFacts const& member, EnemyFacts const& enemy, float reach);

        // The same in seconds of its output (for display).
        float Seconds(WorldModel const& world, PartyMemberFacts const& member, EnemyFacts const& enemy, float reach);
    }
}

#endif
