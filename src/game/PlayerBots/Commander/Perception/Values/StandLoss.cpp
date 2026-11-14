#include "Commander/Perception/Values/StandLoss.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
namespace StandLoss
{
    namespace
    {
        // A member's run speed (the facts have none): the stance modules use the live one.
        float const RUN_SPEED = 7.0f;
    }

    HealerWeights HealerWeightsOf(WorldModel const& world, PartyMemberFacts const& healer, ObjectGuid anchor,
                                  float speed, bool anyInjured)
    {
        HealerWeights w;
        w.speed = std::max(1.0f, speed);
        w.walkFactor = anyInjured ? 1.0f : IDLE_COST_FACTOR;
        w.horizon = std::max(1.0f, std::min(HORIZON_SEC, Value::FightRemainingSec(world)));
        float total = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive || m.guid == healer.guid || m.guid == anchor)
                continue;
            float const taken = Value::TakenDps(world, m.guid);
            w.shares.emplace_back(&m, taken);
            total += taken;
        }
        for (auto& s : w.shares)
            s.second = total > 0.0f ? s.second / total : 1.0f / float(w.shares.size());
        return w;
    }

    bool AnyInjured(WorldModel const& world)
    {
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive && m.healthPct < INJURED_PCT)
                return true;
        return false;
    }

    float HealerStay(WorldModel const& world, PartyMemberFacts const& healer, HealerWeights const& w,
                     std::vector<EnemyFacts const*> const& enemies, Position3 const& p)
    {
        float loss = 0.0f;
        for (auto const& s : w.shares)
            loss += s.second * SpotCost::TargetReach(world, healer, p, s.first->pos, 0.0f, HEAL_RANGE, true,
                                                     w.speed / HEALER_PATH_FACTOR, w.horizon);
        return loss + Exposure::LossAt(world, healer, enemies, p);
    }

    float BestHealerSpot(WorldModel const& world, PartyMemberFacts const& healer, ObjectGuid anchor,
                         Position3 const& anchorAt, std::vector<EnemyFacts const*> const& enemies,
                         float patrolBeforeSec)
    {
        if (!world.field)
            return -1.0f;
        HealerWeights const w = HealerWeightsOf(world, healer, anchor, RUN_SPEED, AnyInjured(world));
        FieldSpots::Query q;
        q.center = anchorAt;
        q.maxYd = HEALER_MAX_YD;
        q.patrolBeforeSec = patrolBeforeSec;
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        float best = -1.0f;
        for (FieldSpots::Spot const& c : FieldSpots::Collect(world, healer, q))
        {
            if (HazardZones::IsInside(zones, HAZARD_HEALER, c.pos))
                continue;
            float const loss = w.walkFactor * SpotCost::Walk(world, healer, c.WalkEstimate(HEALER_PATH_FACTOR) / w.speed) +
                               HealerStay(world, healer, w, enemies, c.pos);
            if (best < 0.0f || loss < best)
                best = loss;
        }
        return best;
    }

    float BestRangedSpot(WorldModel const& world, PartyMemberFacts const& ranged, Position3 const& pileAt,
                         std::vector<EnemyFacts const*> const& enemies, float patrolBeforeSec)
    {
        if (!world.field)
            return -1.0f;
        FieldSpots::Query q;
        q.center = pileAt;
        q.maxYd = RANGED_MAX_YD;
        q.patrolBeforeSec = patrolBeforeSec;
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        float best = -1.0f;
        for (FieldSpots::Spot const& c : FieldSpots::Collect(world, ranged, q))
        {
            if (HazardZones::IsInside(zones, HAZARD_RANGED, c.pos))
                continue;
            float const loss = SpotCost::Walk(world, ranged, c.WalkEstimate(RANGED_PATH_FACTOR) / RUN_SPEED) +
                               Exposure::LossAt(world, ranged, enemies, c.pos);
            if (best < 0.0f || loss < best)
                best = loss;
        }
        return best;
    }
}
}
