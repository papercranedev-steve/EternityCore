/*
 * PartyBot Commander layer 1 (eyes): what a warlock's damage curse on an enemy is worth in
 * points, and what it costs (Perception/Profiles/WarlockCurses.h). Facts and consequences
 * only; CurseBehavior proposes it.
 *
 * Horizon: min(the curse's duration, the enemy's time to die, Value::EnemyTimeToDie).
 * Benefit: every live party member (not pets) that profits from this curse
 * (WarlockCurses::Favoured) deals more by a share of its damage:
 *   Elements / Shadow  damagePct
 *   Recklessness       (1 - armor reduction after the cut) / (1 - before) - 1, at its level
 *                      (Value::ArmorReduction, the enemy's live armor)
 * x Value::ContributionPerSec (one second of its damage in points, as BurstValue) x horizon.
 * Cost: one global cooldown (Value::LostAction); Recklessness also the melee damage its attack
 * power adds on the enemy's victim (MitigationValue::SavedDps) over the horizon.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_CURSE_VALUE_H
#define MANGOS_COMMANDER_CURSE_VALUE_H

#include "Commander/Perception/Profiles/WarlockCurses.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace CurseValue
    {
        float HorizonSec(WorldModel const& world, EnemyFacts const& enemy, CurseEffect const& curse);
        float Benefit(WorldModel const& world, EnemyFacts const& enemy, CurseEffect const& curse);
        float Cost(WorldModel const& world, PartyMemberFacts const& caster, EnemyFacts const& enemy,
                   CurseEffect const& curse);
    }
}

#endif
