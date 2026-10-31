/*
 * PartyBot Commander: value of stopping an enemy cast (value-scoring doc 5.1), in points.
 * Shared by InterruptAssigner (send someone or not) and InterruptBehavior (kick now or not).
 * Pure functions on WorldModel facts plus static spell / creature profiles.
 */

#ifndef MANGOS_COMMANDER_CAST_VALUE_H
#define MANGOS_COMMANDER_CAST_VALUE_H

#include "Common.h"
#include "ObjectGuid.h"

namespace Commander
{
    struct WorldModel;
    struct EnemyFacts;
    struct PartyMemberFacts;
    struct InterruptAbility;

    struct CastValue
    {
        uint32 spellId = 0;
        float points = 0.0f;        // what the party loses if the cast completes
        bool lethal = false;        // the cast would kill its target
    };

    namespace CastValues
    {
        // Fixed values where the effect has no health amount (tunable).
        static constexpr float SUMMON_POINTS = 5.0f;
        static constexpr float OTHER_POINTS = 1.0f;
        // Expected time between two casts of the same enemy, for the cooldown cost.
        static constexpr float CAST_INTERVAL_SEC = 8.0f;
        // Run speed used for the cost of walking into range.
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;

        // Points lost if `enemy` completes `spellId` on `target` (empty = its victim).
        // Hand-written interrupt multipliers (spell and creature) included.
        CastValue Evaluate(WorldModel const& world, EnemyFacts const& enemy, uint32 spellId, ObjectGuid target);

        // Benefit of interrupting the enemy's current cast (or, when it is not casting, its
        // last cast this fight as an estimate for planning), times the chance it lives long
        // enough to complete it.
        CastValue InterruptBenefit(WorldModel const& world, EnemyFacts const& enemy);

        // Cost of `who` using `ability` on `enemy`: its global cooldown, walking into range,
        // and the ability being on cooldown when another enemy casts something worse than
        // this cast (`thisBenefit`).
        float InterruptCost(WorldModel const& world, PartyMemberFacts const& who,
                            InterruptAbility const& ability, EnemyFacts const& enemy, float thisBenefit);
    }
}

#endif
