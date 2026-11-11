#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace SpotCost
{
    float Walk(WorldModel const& world, PartyMemberFacts const& member, float walkSec)
    {
        return Value::LostAction(world, member, std::max(0.0f, walkSec));
    }

    bool Dodges(HazardZone const& zone, float leaveSec)
    {
        return zone.casting && leaveSec + DODGE_SLACK_SEC <= zone.castRemainingSec;
    }

    float ZoneHits(HazardZone const& zone, float horizonSec, float leaveSec)
    {
        if (!zone.casting)
            return horizonSec / zone.intervalSec;
        float const later = std::max(0.0f, horizonSec - zone.castRemainingSec) / zone.intervalSec;
        bool const current = leaveSec < 0.0f || Dodges(zone, leaveSec);
        return later + (current ? 1.0f : 0.0f);
    }

    void AddZoneLoss(WorldModel const& world, HazardZone const& zone, PartyMemberFacts const& member,
                     float hits, HazardLoss& loss)
    {
        float const n = hits * zone.hitChance;
        loss.points += n * HazardZones::HitPoints(world, zone, member);
        loss.damage += n * zone.damage;
    }

    HazardLoss Hazard(WorldModel const& world, PartyMemberFacts const& member, uint8 roleBit,
                      std::vector<HazardZone> const& zones, Position3 const& p, float horizonSec)
    {
        HazardLoss loss;
        for (HazardZone const& z : zones)
            if (z.DangerousTo(roleBit) && HazardZones::DistanceTo(z, p) <= z.radius)
                AddZoneLoss(world, z, member, ZoneHits(z, horizonSec, -1.0f), loss);
        if (loss.damage > 0.0f && loss.damage >= float(member.health))
        {
            loss.lethal = true;
            loss.points += Value::DeathPenalty(member);
        }
        return loss;
    }

    float TargetReach(WorldModel const& world, PartyMemberFacts const& member, Position3 const& at,
                      Position3 const& target, float targetReach, float rangeYd, bool inSight,
                      float speed, float horizonSec)
    {
        float const h = std::max(0.0f, horizonSec);
        if (!inSight)
            return Value::LostAction(world, member, h);
        float const dx = at.x - target.x, dy = at.y - target.y;
        float const excess = std::sqrt(dx * dx + dy * dy) - targetReach - rangeYd;
        if (excess <= 0.0f)
            return 0.0f;
        return Value::LostAction(world, member, std::min(h, excess / std::max(1.0f, speed)));
    }
}
}
