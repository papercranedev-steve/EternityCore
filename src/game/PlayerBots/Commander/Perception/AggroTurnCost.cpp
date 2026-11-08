#include "Commander/Perception/AggroTurnCost.h"
#include "Commander/Perception/LooseEnemy.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/RescueValue.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include <algorithm>

namespace Commander
{
namespace AggroTurnCost
{
    float Of(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& who,
             PartyMemberFacts const& holder, float speed, float gap)
    {
        float const horizon = std::min(HORIZON_SEC, Value::EnemyTimeToDie(world, enemy));
        if (horizon <= 0.0f)
            return 0.0f;

        bool const standsBack = !enemy.meleeEnabled || enemy.rangedDps > 0.0f;
        float const arrival = standsBack ? 0.0f
            : std::max(0.0f, PackResolver::Distance(enemy.pos, who.pos) - enemy.boundingRadius - MELEE_REACH) /
              std::max(0.1f, speed);
        if (arrival >= horizon)
            return 0.0f;
        // Points of the hits on `who` when it is off again after `backSec`.
        auto lossUntil = [&](float backSec)
        {
            float const hits = std::max(0.0f, std::min(backSec, horizon) - arrival);
            return hits > 0.0f ? LooseEnemy::Loss(world, enemy, who, holder, hits) : 0.0f;
        };

        float best = lossUntil(horizon);
        if (!Tanks::IsTank(holder))
            return best;

        float const distance = PackResolver::Distance(holder.pos, enemy.pos);
        float const rate = LooseEnemy::TankRate(world, holder.guid);
        float const walkSec = std::max(0.0f, distance - MELEE_REACH) / RUN_YARDS_PER_SEC;
        if (rate > 1.0f)
            best = std::min(best, lossUntil(walkSec + gap / rate));

        bool immune = false;
        LooseEnemy::Mult(enemy, &immune);
        if (immune || !enemy.InSightOf(holder.guid))
            return best;
        float const enemyDps = RescueValue::EnemyDps(world);
        for (TauntAbility const& t : holder.taunts)
        {
            if (!t.ready)
                continue;
            float const walk = std::max(0.0f, distance - enemy.boundingRadius - t.maxRange) / RUN_YARDS_PER_SEC;
            float const onWho = walk + RescueValue::TauntSecOnVictim(gap, rate, t.holdSec, horizon);
            float const share = std::min(1.0f, (t.cooldownMs / 1000.0f) / RescueValue::RESERVE_HORIZON_SEC);
            float cost = Value::LostAction(world, holder, Value::GCD_SEC + walk) +
                         share * Value::Points(world, enemyDps * RescueValue::TAUNT_RESERVE_SEC);
            if (t.stanceSwap)
                cost += Value::LostAction(world, holder, RescueValue::STANCE_SWAP_COST_SEC);
            best = std::min(best, lossUntil(onWho) + cost);
        }
        return best;
    }
}
}
