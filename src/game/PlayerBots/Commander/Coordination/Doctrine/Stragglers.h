/*
 * PartyBot Commander layer 2 (brain), doctrine step: the stragglers' tactics (battlefield
 * groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md 3, 3.1; user
 * 2026-11-13). Stamps on the members of the straggler groups (Battlefield.h), after the groups:
 *
 *   STAMP_STRAGGLER  a member of a MeleeStraggler group that has not joined the pile: its
 *                    threat matters less, but it is the tank's: it walks to the pile by itself
 *                    (or to whoever it hits: a loose enemy, team rescue); the damage dealers
 *                    leave it (TargetAssigner's focus, DamageTargets) until it joins. Not in the
 *                    Cleanup phase (it is all that is left).
 *   Neither for a player's target or one the party must kill (KillNow, KillOrder, FreeAlly,
 *   SelfDestruct: RoleStamps.h, AllyCharm.h; Apply runs after them).
 *   STAMP_DPS_ONLY   a member of a RangedStraggler group (standing back, not joined) that
 *                    hurts its victim less than the main tank's going for it costs:
 *                      hurt  VictimDamage::Of(its victim, its kill time: health / Value::DealtDps)
 *                      cost  LostAction(tank, GCD) for the taunt, + LostAction(tank, the walk
 *                            into the tank's longest taunt range at RUN_YARDS_PER_SEC) and,
 *                            when it has to walk, the area phase's end cost (plan.area.EndCost(),
 *                            the area phase of the last refresh: AreaPhase runs after this);
 *                    the tank leaves it to the damage dealers (no rescue, RescueAssigner). A
 *                    caster that hurts (hybrids, archers) is rescued as any loose enemy.
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_STRAGGLERS_H
#define MANGOS_COMMANDER_STRAGGLERS_H

#include "Commander/Coordination/TeamPlan.h"

namespace Commander
{
namespace Stragglers
{
    float const RUN_YARDS_PER_SEC = 7.0f;

    struct Verdict
    {
        float hurt = 0.0f;
        float cost = 0.0f;
    };
    // The DPS_ONLY comparison for one enemy standing back (header).
    Verdict TankGoing(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e, PartyMemberFacts const& tank);

    void Apply(WorldModel const& world, TeamPlan const& plan, DoctrinePlan& d);
}
}

#endif
