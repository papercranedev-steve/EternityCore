/*
 * PartyBot Commander layer 1 (eyes): what an enemy hitting someone other than the tank costs,
 * and what the tank winning it back with threat would save. Consequences only; whether the tank
 * goes after it is TargetAssigner's (Coordination/TankTargets.cpp), taunting it ThreatBehavior's.
 * Design: .kiro/docs/2026-11-01-threat-behavior.md section 3, value-scoring doc 5.3.
 *
 * Loss over `seconds` = Value::MoveLoss (what the victim takes on top of the tank: a caster's
 * spells hurt anyone the same, so only death risk counts for them) + Value::HarassLoss (the
 * victim's output lost while a melee enemy chases it: a healer more than a ranged dps).
 * Win back with threat: the tank walks over ((distance - MELEE_REACH) / RUN_YARDS_PER_SEC), then
 * needs gap / tank threat rate to take it back. Both losses are judged over at most
 * HORIZON_SEC and the enemy's time to die.
 */

#ifndef MANGOS_COMMANDER_LOOSE_ENEMY_H
#define MANGOS_COMMANDER_LOOSE_ENEMY_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    struct LooseValue
    {
        float horizon = 0.0f;       // seconds both losses are judged over
        float gap = 0.0f;           // threat the tank is missing
        float walkSec = 0.0f;
        float lossNothing = 0.0f;   // points if nobody takes it
        float lossThreat = 0.0f;    // points until the tank has it back with threat
        bool deadly = false;        // the victim dies within the horizon of everything hitting it
    };

    namespace LooseEnemy
    {
        static constexpr float HORIZON_SEC = 10.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;

        // Points lost while `enemy` hits `victim` instead of `holder` for `seconds`.
        float Loss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim,
                   PartyMemberFacts const& holder, float seconds);
        // Best threat per second `tank` was measured adding to any enemy (0 = none yet).
        float TankRate(WorldModel const& world, ObjectGuid tank);
        // `distance` = tank to enemy (yards).
        LooseValue WinBack(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim,
                           PartyMemberFacts const& tank, float tankRate, float distance);
        // Taunt / win-back weight from the creature override table (tauntMult, 1 = none), and
        // whether it ignores taunts.
        float Mult(EnemyFacts const& enemy, bool* tauntImmune);
    }
}

#endif
