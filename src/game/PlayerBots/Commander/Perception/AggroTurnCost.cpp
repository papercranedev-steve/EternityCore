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
    namespace
    {
        // One turning enemy, prepared for its holder's group.
        struct Item
        {
            Turn turn;
            float horizon = 0.0f;
            float arrival = 0.0f;
            float walkSec = 0.0f;       // the tank to it, into melee
            float distance = 0.0f;
            bool tauntable = false;     // in the tank's sight, not taunt immune
        };

        // Points of the hits on `who` when the enemy is off again after `backSec`.
        float LossUntil(WorldModel const& world, Item const& it, PartyMemberFacts const& who, float backSec)
        {
            float const hits = std::max(0.0f, std::min(backSec, it.horizon) - it.arrival);
            return hits > 0.0f ? LooseEnemy::Loss(world, *it.turn.enemy, who, *it.turn.holder, hits) : 0.0f;
        }

        // The tank wins back the enemies in `items` that are not taunted, one after another.
        float ThreatLoss(WorldModel const& world, std::vector<Item> const& items, std::vector<bool> const& taunted,
                         PartyMemberFacts const& who, float rate)
        {
            std::vector<float> gaps;
            for (size_t i = 0; i < items.size(); ++i)
                if (!taunted[i])
                    gaps.push_back(items[i].turn.gap);
            std::vector<float> const secs = LooseEnemy::SequentialRecovery(gaps, rate);
            float loss = 0.0f;
            size_t k = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (taunted[i])
                    continue;
                float const sec = secs[k++];
                float const back = sec >= ThreatView::NO_LIMIT ? items[i].horizon : items[i].walkSec + sec;
                loss += LossUntil(world, items[i], who, back);
            }
            return loss;
        }

        // The enemies of one tank holder (header).
        float TankGroup(WorldModel const& world, std::vector<Item> const& items, PartyMemberFacts const& who,
                        PartyMemberFacts const& tank)
        {
            float const rate = LooseEnemy::TankRate(world, tank.guid);
            float const enemyDps = RescueValue::EnemyDps(world);
            std::vector<bool> taunted(items.size(), false);
            float tauntLoss = 0.0f;     // loss of the taunted ones + the taunts' cost
            float best = ThreatLoss(world, items, taunted, who, rate);
            for (TauntAbility const& t : tank.taunts)
            {
                if (!t.ready)
                    continue;
                int pick = -1;
                float pickTotal = best + tauntLoss;
                float pickTaunt = 0.0f;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    if (taunted[i] || !items[i].tauntable)
                        continue;
                    Item const& it = items[i];
                    float const walk = std::max(0.0f, it.distance - it.turn.enemy->boundingRadius - t.maxRange) /
                                       RUN_YARDS_PER_SEC;
                    float const onWho = walk + RescueValue::TauntSecOnVictim(it.turn.gap, rate, t.holdSec, it.horizon);
                    float const share = std::min(1.0f, (t.cooldownMs / 1000.0f) / RescueValue::RESERVE_HORIZON_SEC);
                    float cost = Value::LostAction(world, tank, Value::GCD_SEC + walk) +
                                 share * Value::Points(world, enemyDps * RescueValue::TAUNT_RESERVE_SEC);
                    if (t.stanceSwap)
                        cost += Value::LostAction(world, tank, RescueValue::STANCE_SWAP_COST_SEC);
                    float const taunt = LossUntil(world, it, who, onWho) + cost;
                    taunted[i] = true;
                    float const total = ThreatLoss(world, items, taunted, who, rate) + tauntLoss + taunt;
                    taunted[i] = false;
                    if (total < pickTotal)
                    {
                        pick = int(i);
                        pickTotal = total;
                        pickTaunt = taunt;
                    }
                }
                if (pick < 0)
                    continue;
                taunted[pick] = true;
                tauntLoss += pickTaunt;
                best = ThreatLoss(world, items, taunted, who, rate);
            }
            return best + tauntLoss;
        }
    }

    float OfGroup(WorldModel const& world, std::vector<Turn> const& turns, PartyMemberFacts const& who)
    {
        float loss = 0.0f;
        // Grouped by holder: the enemies of one tank share its threat rate and its taunts.
        std::vector<std::pair<PartyMemberFacts const*, std::vector<Item>>> groups;
        for (Turn const& t : turns)
        {
            if (!t.enemy || !t.holder)
                continue;
            Item it;
            it.turn = t;
            it.horizon = std::min(HORIZON_SEC, Value::EnemyTimeToDie(world, *t.enemy));
            if (it.horizon <= 0.0f)
                continue;
            bool const standsBack = !t.enemy->meleeEnabled || t.enemy->rangedDps > 0.0f;
            it.arrival = standsBack ? 0.0f
                : std::max(0.0f, PackResolver::Distance(t.enemy->pos, who.pos) - t.enemy->boundingRadius - MELEE_REACH) /
                  std::max(0.1f, t.speed);
            if (it.arrival >= it.horizon)
                continue;
            if (!Tanks::IsTank(*t.holder))
            {
                loss += LossUntil(world, it, who, it.horizon);      // nobody takes it back
                continue;
            }
            it.distance = PackResolver::Distance(t.holder->pos, t.enemy->pos);
            it.walkSec = std::max(0.0f, it.distance - MELEE_REACH) / RUN_YARDS_PER_SEC;
            bool immune = false;
            LooseEnemy::Mult(*t.enemy, &immune);
            it.tauntable = !immune && t.enemy->InSightOf(t.holder->guid);
            auto group = std::find_if(groups.begin(), groups.end(),
                [&t](std::pair<PartyMemberFacts const*, std::vector<Item>> const& g) { return g.first == t.holder; });
            if (group == groups.end())
            {
                groups.push_back({ t.holder, {} });
                group = groups.end() - 1;
            }
            group->second.push_back(it);
        }
        for (auto const& g : groups)
            loss += TankGroup(world, g.second, who, *g.first);
        return loss;
    }
}
}
