/*
 * PartyBot Commander layer 1 (eyes): what a healer or a ranged damage dealer loses standing at
 * a cell, and its best cell around a pile (tank fight spot doc
 * .kiro/docs/2026-11-14-tank-fight-spot.md 3.6, step 6). The healer's terms were
 * HealerStanceBehavior's (StayLoss, Weights, 2026-11-11 spot scoring step 3); the stance
 * modules and the fight spot ("how well the party can stand around S") use the same ones.
 *
 * Healer at p, anchor A (the tank):
 *   shares  every other living member but A: its share of Value::TakenDps (even when nobody
 *           takes damage);
 *   stay    sum over them of share x SpotCost::TargetReach(p -> member, HEAL_RANGE, in sight by
 *           PositionField::SeesEstimate (since 2026-11-15), walk at speed / HEALER_PATH_FACTOR,
 *           horizon) + Exposure::LossAt(enemies, p);
 *   cell    walkFactor x SpotCost::Walk(grid walk x HEALER_PATH_FACTOR / speed) + stay;
 *           walkFactor IDLE_COST_FACTOR while nobody is injured (below INJURED_PCT).
 * Ranged at p: SpotCost::Walk(grid walk x RANGED_PATH_FACTOR / speed) + Exposure::LossAt.
 * Best*: the lowest over the position field's cells (FieldSpots::Collect, `patrolBeforeSec`)
 * within HEALER_MAX_YD of A / RANGED_MAX_YD of the pile, out of the role's hazard areas; -1 =
 * none. No line of sight or path check (the stance modules do those on their choice).
 * Pure (facts).
 */

#ifndef MANGOS_COMMANDER_STAND_LOSS_H
#define MANGOS_COMMANDER_STAND_LOSS_H

#include "Commander/Perception/WorldModel.h"
#include <utility>
#include <vector>

namespace Commander
{
namespace StandLoss
{
    float const HEAL_RANGE = 28.0f;
    float const HEALER_MAX_YD = 24.0f;
    float const HEALER_PATH_FACTOR = 1.2f;
    float const IDLE_COST_FACTOR = 0.25f;
    float const INJURED_PCT = 80.0f;
    float const HORIZON_SEC = 8.0f;
    float const RANGED_MAX_YD = 26.0f;
    float const RANGED_PATH_FACTOR = 1.2f;

    struct HealerWeights
    {
        float speed = 7.0f;
        float walkFactor = 1.0f;
        float horizon = 1.0f;
        std::vector<std::pair<PartyMemberFacts const*, float>> shares;
    };

    HealerWeights HealerWeightsOf(WorldModel const& world, PartyMemberFacts const& healer, ObjectGuid anchor,
                                  float speed, bool anyInjured);
    // Some living member below INJURED_PCT.
    bool AnyInjured(WorldModel const& world);
    float HealerStay(WorldModel const& world, PartyMemberFacts const& healer, HealerWeights const& w,
                     std::vector<EnemyFacts const*> const& enemies, Position3 const& p);

    float BestHealerSpot(WorldModel const& world, PartyMemberFacts const& healer, ObjectGuid anchor,
                         Position3 const& anchorAt, std::vector<EnemyFacts const*> const& enemies,
                         float patrolBeforeSec);
    float BestRangedSpot(WorldModel const& world, PartyMemberFacts const& ranged, Position3 const& pileAt,
                         std::vector<EnemyFacts const*> const& enemies, float patrolBeforeSec);
}
}

#endif
