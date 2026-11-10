#include "Commander/Coordination/Doctrine/AllyCharm.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace AllyCharm
{
    void Apply(WorldModel const& world, DoctrinePlan& d)
    {
        d.charmed.clear();
        float const dps = std::max(1.0f, Value::DealtDps(world));
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.charmed || m.isPet || !m.alive || m.charmer.IsEmpty())
                continue;
            EnemyFacts const* charmer = world.FindEnemy(m.charmer);
            CharmedAlly c;
            c.ally = m.guid;
            c.charmer = m.charmer;
            c.charmLeftMs = m.charmLeftMs;
            c.killSec = charmer ? float(charmer->health) / dps : 0.0f;
            float const leftSec = m.charmLeftMs ? m.charmLeftMs / 1000.0f : c.killSec;
            c.hold = !charmer || std::min(c.killSec, leftSec) > QUICK_KILL_SEC;
            d.charmed.push_back(c);
            if (charmer)
                for (DoctrineEnemy& x : d.enemies)
                    if (x.guid == charmer->guid)
                        x.stamps |= STAMP_FREE_ALLY;
        }
    }
}
}
