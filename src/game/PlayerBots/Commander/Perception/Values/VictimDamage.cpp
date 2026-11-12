#include "Commander/Perception/Values/VictimDamage.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
namespace VictimDamage
{
    PartyMemberFacts const* NearestSoft(WorldModel const& world, Position3 const& at)
    {
        PartyMemberFacts const* best = nullptr;
        float bestYd = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive || m.isPet || Tanks::IsTank(m))
                continue;
            float const yd = PackResolver::Distance(m.pos, at);
            if (!best || yd < bestYd)
            {
                best = &m;
                bestYd = yd;
            }
        }
        return best;
    }

    Hit Of(WorldModel const& world, EnemyFacts const& enemy, float sec)
    {
        Hit h;
        h.victim = world.FindMember(enemy.victim);
        if (!h.victim || !h.victim->alive)
            h.victim = NearestSoft(world, enemy.pos);
        if (!h.victim)
            return h;
        h.dps = FightLoad::FoeOf(enemy, *h.victim).dps;
        h.points = Value::Points(world, h.dps * std::max(0.0f, sec));
        return h;
    }

    Hit Of(WorldModel const& world, PackCreature const& creature, float sec)
    {
        Hit h;
        h.victim = NearestSoft(world, creature.pos);
        if (!h.victim)
            return h;
        float const melee = creature.meleeDps * (1.0f - Value::ArmorReduction(h.victim->armor, creature.level));
        h.dps = std::max(melee, creature.rangedDps);
        h.points = Value::Points(world, h.dps * std::max(0.0f, sec));
        return h;
    }
}
}
