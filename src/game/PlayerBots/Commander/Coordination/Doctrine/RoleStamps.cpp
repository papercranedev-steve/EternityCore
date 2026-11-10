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
        uint16 const roles = Tags::RolesOf(e.entry);
        x.source = uint8(source);
        x.roles = roles;

        if (source == Source::Trap)
            x.stamps |= STAMP_SWARM | STAMP_NO_TAUNT;
        if (roles & TAG_ALARM)
            x.stamps |= STAMP_ALARM;
        if (roles & (TAG_TAUNT_IMMUNE | TAG_TOTEM))
            x.stamps |= STAMP_NO_TAUNT;
        // Kill order (user 2026-11-10, "治疗怪、召唤怪先杀"): healers, summoners, totems first.
        if (roles & (TAG_HEALER | TAG_SUMMONER | TAG_TOTEM))
            x.stamps |= STAMP_KILL_ORDER;

        // A timed blast (Broken Cadaver). A walking bomb (no timer, it blows when it reaches
        // someone) is the intercept's (FocusAssigner).
        if (!(roles & TAG_SELF_DESTRUCT))
            return;
        BlastFacts const blast = Tags::BlastOf(e);
        if (!blast.valid || !blast.latestMs)
            return;
        x.stamps |= STAMP_SELF_DESTRUCT | STAMP_NO_TAUNT;
        float const secLeft = (int32(blast.earliestMs) - int32(world.refreshedAtMs)) / 1000.0f;
        float const killSec = float(e.health) / std::max(1.0f, Value::DealtDps(world));
        if (killSec + KILL_MARGIN_SEC < secLeft)
            x.stamps |= STAMP_KILL_NOW;
        else
            x.stamps |= STAMP_STAND_CLEAR;
    }
}
}
