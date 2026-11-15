/*
 * PartyBot Commander layer 1 (eyes): what an attack power debuff around a tank bot
 * (PartyMemberFacts::mitigations: Demoralizing Shout / Roar) is worth in points, and what it
 * costs. Facts and consequences only; MitigationBehavior proposes it.
 *
 * A creature's weapon damage scales by F(ap) = 0.7 + 0.3 x ap / apDb (Creature::
 * UpdateDamagePhysical; apDb 0 = no scaling, nothing to gain). EnemyFacts::meleeDps is live, so it
 * is the damage at F(apNow); taking `d` attack power off saves meleeDps x 0.3 x d / apDb / F(apNow).
 *
 * Benefit, per acting enemy within the spell's radius of the caster that hits a party member:
 * the melee damage it would not deal on that victim (armor included, Value::HitDps; ranged
 * spells are not attack power), over min(duration, its time to die). The debuffs do not stack
 * (assumed: only the strongest counts), so with one on it (apDebuff, apDebuffMs) only the part
 * above it counts until it runs out, all of it after. Points(sum).
 * Not at all while a creature out of combat is in reach (PullsIdle: it would pull it).
 * Cost: one global cooldown (Value::LostAction) plus the rage as the share of a threat skill it
 * would have paid for (rageCost / RAGE_PER_ACTION global cooldowns): rage has no other price.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_MITIGATION_VALUE_H
#define MANGOS_COMMANDER_MITIGATION_VALUE_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace MitigationValue
    {
        // Rage of one threat skill (Sunder Armor, Maul: 15 rage), power units. Untested guess.
        static constexpr float RAGE_PER_ACTION = 150.0f;

        // Damage it takes off over its duration, in points. `caster` is where it is cast from.
        float Benefit(WorldModel const& world, MitigationAbility const& a, Position3 const& caster);
        // A creature not in combat (a pack, a patrol, neutral ones too) is within the radius of
        // `caster` + IDLE_MARGIN_YD (their bounding radius is not in the facts): the shout would
        // hit it and pull it.
        static constexpr float IDLE_MARGIN_YD = 2.0f;
        bool PullsIdle(WorldModel const& world, MitigationAbility const& a, Position3 const& caster);
        // A global cooldown and the rage (above).
        float Cost(WorldModel const& world, PartyMemberFacts const& caster, MitigationAbility const& a);
        // Melee damage per second on `victim` that `apOff` attack power less takes off (0 without
        // apDb, or when it does not hit in melee).
        float SavedDps(EnemyFacts const& e, PartyMemberFacts const& victim, float apOff);
    }
}

#endif
