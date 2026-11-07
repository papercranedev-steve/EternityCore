#include "Commander/Perception/LooseEnemy.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/CreatureProfile.h"
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

    float Mult(EnemyFacts const& enemy, bool* tauntImmune)
    {
        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(enemy.entry);
        if (tauntImmune)
            *tauntImmune = p && p->tauntImmune;
        return (p && p->hasOverride) ? p->override.tauntMult : 1.0f;
    }
}
}
