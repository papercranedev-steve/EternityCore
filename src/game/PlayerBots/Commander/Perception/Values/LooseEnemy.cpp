#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include <algorithm>

namespace Commander
{
namespace LooseEnemy
{
    float Loss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim,
               PartyMemberFacts const& holder, float seconds)
    {
        return Value::MoveLoss(world, enemy, victim, holder, seconds) +
               Value::HarassLoss(world, enemy, victim, seconds);
    }

    float ThinLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& taker,
                   PartyMemberFacts const& holder, float runwaySec, float horizonSec)
    {
        if (horizonSec <= 0.0f || runwaySec >= horizonSec)
            return 0.0f;
        float const soon = 1.0f - std::max(0.0f, runwaySec) / horizonSec;
        return Loss(world, enemy, taker, holder, THIN_RECLAIM_SEC) * THIN_WEIGHT * soon;
    }

    float TankRate(WorldModel const& world, ObjectGuid tank)
    {
        float rate = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (ThreatEntry const* t = ThreatView::FindEntry(e, tank))
                rate = std::max(rate, t->threatPerSec);
        return rate;
    }

    LooseValue WinBack(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim,
                       PartyMemberFacts const& tank, float tankRate, float distance)
    {
        LooseValue v;
        v.gap = std::max(0.0f, ThreatView::Headroom(enemy, tank.guid));
        float const recovery = tankRate > 1.0f ? v.gap / tankRate : ThreatView::NO_LIMIT;
        v.horizon = std::min(HORIZON_SEC, Value::EnemyTimeToDie(world, enemy));
        v.walkSec = std::max(0.0f, distance - MELEE_REACH) / RUN_YARDS_PER_SEC;
        v.lossNothing = Loss(world, enemy, victim, tank, v.horizon);
        v.lossThreat = Loss(world, enemy, victim, tank, std::min(v.walkSec + recovery, v.horizon));
        v.deadly = Value::SurviveAllSec(world, enemy, victim) < v.horizon;
        return v;
    }

    std::vector<float> SequentialRecovery(std::vector<float> const& gaps, float rate)
    {
        std::vector<float> secs(gaps.size(), ThreatView::NO_LIMIT);
        if (rate <= 1.0f)
            return secs;
        std::vector<size_t> order(gaps.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&gaps](size_t a, size_t b) { return gaps[a] < gaps[b]; });
        float done = 0.0f;
        for (size_t i : order)
        {
            done += std::max(0.0f, gaps[i]);
            secs[i] = done / rate;
        }
        return secs;
    }

    float Mult(EnemyFacts const& enemy, bool* tauntImmune)
    {
        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(enemy.entry);
        if (tauntImmune)
            *tauntImmune = p && p->tauntImmune;
        return (p && p->hasOverride) ? p->override.tauntMult : 1.0f;
    }
}
}
