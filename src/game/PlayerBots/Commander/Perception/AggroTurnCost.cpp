#include "Commander/Perception/AggroTurnCost.h"
#include "Commander/Perception/LooseEnemy.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/RescueValue.h"
#include "Commander/Perception/SurvivalValue.h"
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
        // One turning enemy.
        struct Item
        {
            Turn turn;
            float horizon = 0.0f;
            float arrival = 0.0f;
            float walkSec = 0.0f;       // the tank to it, into melee
            float distance = 0.0f;
            float dps = 0.0f;           // its hits on `who` (damage taken factor included)
            float extraDps = 0.0f;      // beyond what its holder took from it
            bool tauntable = false;     // in the tank's sight, not taunt immune
            int group = -1;             // index in the tank groups, -1 = nobody takes it back
        };

        struct Group
        {
            PartyMemberFacts const* tank = nullptr;
            float rate = 0.0f;
            std::vector<size_t> items;
        };

        struct Judge
        {
            WorldModel const& world;
            PartyMemberFacts const& who;
            Position3 const at;
            float health = 0.0f;
            std::vector<Item> items;
            std::vector<Group> groups;
            float baseDps = 0.0f;
            float baseRisk = 0.0f;

            Judge(WorldModel const& w, PartyMemberFacts const& m, Position3 const& p) : world(w), who(m), at(p) {}

            // Seconds each enemy is on `who`, the taunted ones (end >= 0) fixed.
            std::vector<float> Ends(std::vector<float> const& tauntEnd) const
            {
                std::vector<float> ends(items.size(), 0.0f);
                for (size_t i = 0; i < items.size(); ++i)
                    ends[i] = tauntEnd[i] >= 0.0f ? tauntEnd[i] : items[i].horizon;
                for (Group const& g : groups)
                {
                    std::vector<float> gaps;
                    std::vector<size_t> which;
                    for (size_t i : g.items)
                        if (tauntEnd[i] < 0.0f)
                        {
                            gaps.push_back(items[i].turn.gap);
                            which.push_back(i);
                        }
                    std::vector<float> const secs = LooseEnemy::SequentialRecovery(gaps, g.rate);
                    for (size_t k = 0; k < which.size(); ++k)
                    {
                        Item const& it = items[which[k]];
                        float const back = secs[k] >= ThreatView::NO_LIMIT ? it.horizon : it.walkSec + secs[k];
                        ends[which[k]] = std::min(back, it.horizon);
                    }
                }
                return ends;
            }

            float Total(std::vector<float> const& ends) const
            {
                float loss = 0.0f;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    float const hits = std::max(0.0f, ends[i] - items[i].arrival);
                    if (hits <= 0.0f)
                        continue;
                    loss += Value::Points(world, items[i].extraDps * hits) +
                            Value::HarassLoss(world, *items[i].turn.enemy, who, hits);
                }
                // Together: can they kill `who`?
                float demand = baseDps;
                for (Item const& it : items)
                    demand += it.dps;
                SurvivalValue::HealIn const heal = SurvivalValue::HealOn(world, who, demand);
                float h = health;
                float ttd = ThreatView::NO_LIMIT;
                float const cap = float(who.maxHealth) + (health - float(who.health));
                for (float t = 0.0f; t < SurvivalValue::HORIZON_SEC; t += STEP_SEC)
                {
                    float dps = baseDps;
                    for (size_t i = 0; i < items.size(); ++i)
                        if (t >= items[i].arrival && t < ends[i])
                            dps += items[i].dps;
                    float const healing = t >= heal.delaySec ? heal.hps : 0.0f;
                    h = std::min(cap, h - (dps - healing) * STEP_SEC);
                    if (h <= 0.0f)
                    {
                        ttd = t + STEP_SEC;
                        break;
                    }
                }
                float const risk = SurvivalValue::Risk(ttd);
                if (risk > baseRisk)
                    loss += Value::DeathPenalty(who) * (risk - baseRisk);
                return loss;
            }
        };

        bool Busy(Options const& o, ObjectGuid tank)
        {
            return std::find(o.busyTanks.begin(), o.busyTanks.end(), tank) != o.busyTanks.end();
        }
    }

    float OfGroup(WorldModel const& world, std::vector<Turn> const& turns, PartyMemberFacts const& who,
                  Options const& options)
    {
        Judge j(world, who, options.at ? *options.at : who.pos);
        j.health = float(who.health) + who.absorb + options.extraAbsorb;
        j.baseDps = SurvivalValue::Demand(world, who);
        j.baseRisk = SurvivalValue::Risk(SurvivalValue::TimeToDieUnder(j.health, j.baseDps,
                                         SurvivalValue::HealOn(world, who, j.baseDps)));

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
                : std::max(0.0f, PackResolver::Distance(t.enemy->pos, j.at) - t.enemy->boundingRadius - MELEE_REACH) /
                  std::max(0.1f, t.speed);
            if (it.arrival >= it.horizon)
                continue;
            float const onWho = Value::HitDps(*t.enemy, who);
            it.dps = onWho * who.damageTakenMult;
            it.extraDps = std::max(0.0f, onWho - Value::HitDps(*t.enemy, *t.holder));
            if (Tanks::IsTank(*t.holder) && !Busy(options, t.holder->guid))
            {
                it.distance = PackResolver::Distance(t.holder->pos, t.enemy->pos);
                it.walkSec = std::max(0.0f, it.distance - MELEE_REACH) / RUN_YARDS_PER_SEC;
                bool immune = false;
                LooseEnemy::Mult(*t.enemy, &immune);
                it.tauntable = !immune && t.enemy->InSightOf(t.holder->guid);
                auto g = std::find_if(j.groups.begin(), j.groups.end(),
                    [&t](Group const& x) { return x.tank == t.holder; });
                if (g == j.groups.end())
                {
                    Group x;
                    x.tank = t.holder;
                    x.rate = LooseEnemy::TankRate(world, t.holder->guid);
                    j.groups.push_back(x);
                    g = j.groups.end() - 1;
                }
                it.group = int(g - j.groups.begin());
                g->items.push_back(j.items.size());
            }
            j.items.push_back(it);
        }
        if (j.items.empty())
            return 0.0f;

        std::vector<float> tauntEnd(j.items.size(), -1.0f);
        float tauntCost = 0.0f;
        float best = j.Total(j.Ends(tauntEnd));
        if (!options.noTaunts)
        {
            float const enemyDps = RescueValue::EnemyDps(world);
            for (Group const& g : j.groups)
            {
                for (TauntAbility const& t : g.tank->taunts)
                {
                    if (!t.ready)
                        continue;
                    int pick = -1;
                    float pickTotal = best;
                    float pickEnd = 0.0f;
                    float pickCost = 0.0f;
                    for (size_t i : g.items)
                    {
                        Item const& it = j.items[i];
                        if (tauntEnd[i] >= 0.0f || !it.tauntable)
                            continue;
                        float const walk = std::max(0.0f, it.distance - it.turn.enemy->boundingRadius - t.maxRange) /
                                           RUN_YARDS_PER_SEC;
                        float const end = std::min(it.horizon,
                            walk + RescueValue::TauntSecOnVictim(it.turn.gap, g.rate, t.holdSec, it.horizon));
                        float const share = std::min(1.0f, (t.cooldownMs / 1000.0f) / RescueValue::RESERVE_HORIZON_SEC);
                        float cost = Value::LostAction(world, *g.tank, Value::GCD_SEC + walk) +
                                     share * Value::Points(world, enemyDps * RescueValue::TAUNT_RESERVE_SEC);
                        if (t.stanceSwap)
                            cost += Value::LostAction(world, *g.tank, RescueValue::STANCE_SWAP_COST_SEC);
                        tauntEnd[i] = end;
                        float const total = j.Total(j.Ends(tauntEnd)) + tauntCost + cost;
                        tauntEnd[i] = -1.0f;
                        if (total < pickTotal)
                        {
                            pick = int(i);
                            pickTotal = total;
                            pickEnd = end;
                            pickCost = cost;
                        }
                    }
                    if (pick < 0)
                        continue;
                    tauntEnd[pick] = pickEnd;
                    tauntCost += pickCost;
                    best = pickTotal;
                }
            }
        }
        return best;
    }
}
}
