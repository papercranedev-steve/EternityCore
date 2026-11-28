#include "Commander/Perception/Spatial/MoveNeed.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"

namespace Commander
{
namespace MoveNeed
{
    namespace
    {
        // Gap from `pos` to the enemy's edge.
        float GapTo(EnemyFacts const& e, Position3 const& pos)
        {
            return PackResolver::Distance(e.pos, pos) - e.boundingRadius;
        }

        bool Fights(EnemyFacts const& e)
        {
            return e.acting && !e.crowdControlled && !e.victim.IsEmpty();
        }
    }

    MoveReason Of(WorldModel const& world, PartyMemberFacts const& m)
    {
        if (!m.alive || m.isPet)
            return MoveReason::None;

        bool const melee = m.role == ROLE_MELEE_DPS || m.role == ROLE_TANK;
        if (!melee && Value::IncomingOn(world, m.guid) > 0.0f)
            return MoveReason::Melee;

        if (HazardZones::IsInside(HazardZones::Get(world), HazardZones::RoleBit(m.role), m.pos, m.guid))
            return MoveReason::Hazard;

        if (melee)
        {
            if (EnemyFacts const* victim = world.FindEnemy(m.victim))
                if (Fights(*victim) && GapTo(*victim, m.pos) > MELEE_REACH_YD)
                    return MoveReason::Reach;
            if (m.role == ROLE_TANK)
            {
                for (EnemyFacts const& e : world.enemies)
                {
                    if (!Fights(e) || e.victim == m.guid || GapTo(e, m.pos) <= MELEE_REACH_YD)
                        continue;
                    PartyMemberFacts const* hit = world.FindMember(e.victim);
                    if (hit && hit->role != ROLE_TANK)
                        return MoveReason::Reach;
                }
            }
        }

        if (m.HealsTeam())
        {
            for (PartyMemberFacts const& o : world.party)
            {
                if (o.guid == m.guid || !o.alive || o.isPet)
                    continue;
                if (Value::TakenDps(world, o.guid) > 0.0f && PackResolver::Distance(m.pos, o.pos) > HEAL_RANGE_YD)
                    return MoveReason::HealReach;
            }
        }
        return MoveReason::None;
    }

    char const* Name(MoveReason reason)
    {
        switch (reason)
        {
            case MoveReason::Melee:     return "melee on it";
            case MoveReason::Hazard:    return "in a hazard";
            case MoveReason::Reach:     return "target out of reach";
            case MoveReason::HealReach: return "heal target out of range";
            default:                    return "none";
        }
    }
}
}
