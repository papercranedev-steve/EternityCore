/*
 * PartyBot Commander layer 1 (eyes): what a stun, root or slow on an enemy about to run away
 * is worth, in points (flee interception, todo 13, .kiro/docs/2026-11-08-flee-interception.md).
 * The fallback after the focus (user, 2026-11-08): a control is only worth it when the party
 * on the enemy still lets it run far (FleeFacts::focusedPoints > 0). Facts only: who casts is
 * FleeStopAssigner's.
 *
 * A stun or root does not end the run, it pauses it (FleePredictor.h). So:
 *   hold (stun / root)  only when the party kills it within holdSec once it is at the flee line
 *                       (HealthAtRun <= Value::DealtDps x holdSec): benefit = focusedPoints
 *                       (that run never happens); else 0
 *   slow                benefit = focusedPoints - PointsWithin(run speed x (1 - slowPct) x
 *                       runKillSec): the focused run gets shorter
 * Cost:
 *   action    Value::LostAction(max(cast time, GCD))
 *   reach     LostAction(walk into range / run speed), walk = distance - range (0 in range;
 *             an area spell around the caster: its radius)
 *   mana      ManaWorth::Points(mana cost)
 *   cooldown  ControlValue::COOLDOWN_POINTS_PER_MIN x min(cooldown, fight left) / 60 for
 *             cooldowns longer than a GCD
 * Holding the spell until the trigger (TeamPlan::HoldsSpell) is not counted.
 */

#ifndef MANGOS_COMMANDER_FLEE_STOP_VALUE_H
#define MANGOS_COMMANDER_FLEE_STOP_VALUE_H

#include "Commander/Perception/WorldModel.h"
#include <string>

namespace Commander
{
namespace FleeStopValue
{
    struct Worth
    {
        float benefit = 0.0f;
        float action = 0.0f;
        float reach = 0.0f;
        float mana = 0.0f;
        float cooldown = 0.0f;

        float Cost() const { return action + reach + mana + cooldown; }
        float Net() const { return benefit - Cost(); }
    };

    // A hold that kills it before it ends, once it is at the flee line.
    bool KillsInHold(WorldModel const& world, EnemyFacts const& e, FleeStopAbility const& a);

    Worth Of(WorldModel const& world, PartyMemberFacts const& member, FleeStopAbility const& a, EnemyFacts const& e);

    // "net +2.1 (benefit 3.0 | action 0.4 reach 0.0 mana 0.2 cooldown 0.3)"
    std::string Describe(Worth const& w);
}
}

#endif
