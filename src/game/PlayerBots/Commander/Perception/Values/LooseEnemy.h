/*
 * PartyBot Commander layer 1 (eyes): what an enemy hitting someone other than the tank costs,
 * and what the tank winning it back with threat would save. Consequences only; whether the tank
 * goes after it, taunts it or leaves it to the victim's Fade is the team rescue's (RescueValue.h, RescueAssigner).
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
#include <vector>

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

        // Who a loose enemy hits: a member, or an ally the party protects (AllyFacts::protect,
        // boss doc G3: no harassment loss, it does not cast for us).
        struct Victim
        {
            PartyMemberFacts const* member = nullptr;
            AllyFacts const* ally = nullptr;
            Victim(PartyMemberFacts const& m) : member(&m) {}
            Victim(AllyFacts const& a) : ally(&a) {}
            ObjectGuid Guid() const { return member ? member->guid : ally->guid; }
        };

        // Points lost while `enemy` hits `victim` instead of `holder` for `seconds`.
        float Loss(WorldModel const& world, EnemyFacts const& enemy, Victim const& victim,
                   PartyMemberFacts const& holder, float seconds);

        // (A thin lead still on the tank: ThreatRisk.h, since 2026-11-13; before, ThinLoss here.)
        // Best threat per second `tank` was measured adding to any enemy (0 = none yet).
        float TankRate(WorldModel const& world, ObjectGuid tank);
        // `distance` = tank to enemy (yards).
        LooseValue WinBack(WorldModel const& world, EnemyFacts const& enemy, Victim const& victim,
                           PartyMemberFacts const& tank, float tankRate, float distance);
        // Taunt / win-back weight from the creature override table (tauntMult, 1 = none), and
        // whether it ignores taunts.
        float Mult(EnemyFacts const& enemy, bool* tauntImmune);

        // Several enemies one tank has to win back with threat at once: it hits one at a time,
        // so its `rate` serves them one after another, the smallest gap first (the order that
        // keeps the total time loose lowest). Seconds until each is back, in the order of
        // `gaps` (walking not included); ThreatView::NO_LIMIT when rate <= 1. Before
        // 2026-11-08 every enemy was counted as won back at the full rate at the same time
        // (strat-20261108-141921: six enemies turning on a Blizzard cost as little as one,
        // eleven Demoralizing Shouts in 30 s).
        std::vector<float> SequentialRecovery(std::vector<float> const& gaps, float rate);
    }
}

#endif
