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
        // Kill order (user 2026-11-10, "治疗怪、召唤怪先杀"; mana drainers "非常讨厌"; resurrectors):
        // the points pick among them (a fragile one first).
        if (roles & (TAG_HEALER | TAG_SUMMONER | TAG_TOTEM | TAG_MANA_DRAIN | TAG_RESURRECTS))
        {
            x.stamps |= STAMP_KILL_ORDER;
            x.killTier = KILL_TIER_ROLE;
        }

        // A damage shield / spell reflection on it (user 2026-11-10: everyone stops, melee too):
        // nobody hits it while it lasts. Alone in the fight with a long one (Thorns Aura 60 s):
        // fought through, else it could never be killed.
        if (e.reflectMs)
        {
            bool const otherFree = std::any_of(world.enemies.begin(), world.enemies.end(), [&e](EnemyFacts const& o)
            {
                return o.guid != e.guid && o.acting && o.health && !o.onAllyOnly && !o.crowdControlled && !o.reflectMs;
            });
            if (otherFree || e.reflectMs <= REFLECT_ALONE_MAX_MS)
                x.stamps |= STAMP_HOLD_FIRE;
        }
        // A member is taming it (a hunter's taming rod or Tame Beast): it must live until the
        // channel ends, alone or not (it hits only the tamer meanwhile; the healer still heals).
        if (e.beingTamed)
            x.stamps |= STAMP_HOLD_FIRE | STAMP_NO_TAUNT;

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
