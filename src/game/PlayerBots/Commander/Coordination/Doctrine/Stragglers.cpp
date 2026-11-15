#include "Commander/Coordination/Doctrine/Stragglers.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/VictimDamage.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>

namespace Commander
{
namespace Stragglers
{
    Verdict TankGoing(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e, PartyMemberFacts const& tank)
    {
        Verdict v;
        float const killSec = float(e.health) / std::max(1.0f, Value::DealtDps(world));
        v.hurt = VictimDamage::Of(world, e, killSec).points;
        float range = 0.0f;
        for (TauntAbility const& t : tank.taunts)
            range = std::max(range, t.maxRange);
        float const walkSec = std::max(0.0f, PackResolver::Distance(tank.pos, e.pos) - e.boundingRadius - range) /
                              RUN_YARDS_PER_SEC;
        v.cost = Value::LostAction(world, tank, Value::GCD_SEC) + Value::LostAction(world, tank, walkSec) +
                 (walkSec > 0.0f ? plan.area.EndCost() : 0.0f);
        return v;
    }

    void Apply(WorldModel const& world, TeamPlan const& plan, DoctrinePlan& d)
    {
        PartyMemberFacts const* tank = Tanks::Main(world);
        if (!tank || d.phase == FightPhase::Cleanup)
            return;
        for (DoctrineEnemy& x : d.enemies)
        {
            if (x.group < 0 || size_t(x.group) >= d.groups.size())
                continue;
            DoctrineGroup const& g = d.groups[x.group];
            // Joined, a player's target, or one the party must kill (its role stamps first).
            if (g.Joined(x.guid) || (x.stamps & (STAMP_PLAYER_TARGET | STAMP_KILL_NOW | STAMP_KILL_ORDER |
                                                 STAMP_FREE_ALLY | STAMP_SELF_DESTRUCT)))
                continue;
            EnemyFacts const* e = world.FindEnemy(x.guid);
            if (!e)
                continue;
            if (g.kind == GroupKind::MeleeStraggler)
                x.stamps |= STAMP_STRAGGLER;
            else if (g.kind == GroupKind::RangedStraggler && (x.stamps & STAMP_OUTLIER))
            {
                Verdict const v = TankGoing(world, plan, *e, *tank);
                if (Trace::On())
                    Trace::Emit("straggler", Trace::Guid(e->guid).c_str(),
                        Trace::Format("{\"hurt\":%.2f,\"cost\":%.2f}", v.hurt, v.cost));
                if (v.hurt <= v.cost)
                    x.stamps |= STAMP_DPS_ONLY;
            }
        }
    }
}
}
