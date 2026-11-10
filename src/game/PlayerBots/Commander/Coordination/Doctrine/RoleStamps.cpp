#include "Commander/Coordination/Doctrine/RoleStamps.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace RoleStamps
{
    void Apply(WorldModel const& world, EnemyFacts const& e, DoctrineEnemy& x)
    {
        Source const source = Tags::SourceOf(e);
        uint8 const roles = Tags::RolesOf(e.entry);
        x.source = uint8(source);
        x.roles = roles;

        if (source == Source::Trap)
            x.stamps |= STAMP_SWARM;
        if (roles & TAG_ALARM)
            x.stamps |= STAMP_ALARM;
        if (!(roles & TAG_SELF_DESTRUCT))
            return;
        x.stamps |= STAMP_SELF_DESTRUCT;

        BlastFacts const blast = Tags::BlastOf(e);
        if (!blast.valid || !blast.latestMs)
            return;
        float const secLeft = (int32(blast.earliestMs) - int32(world.refreshedAtMs)) / 1000.0f;
        float const killSec = float(e.health) / std::max(1.0f, Value::DealtDps(world));
        if (killSec + KILL_MARGIN_SEC < secLeft)
            x.stamps |= STAMP_KILL_NOW;
        else
            x.stamps |= STAMP_STAND_CLEAR;
    }
}
}
