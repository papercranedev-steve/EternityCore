#include "Commander/Perception/Tanks.h"
#include <algorithm>

namespace Commander
{
namespace Tanks
{
    bool IsTank(PartyMemberFacts const& m)
    {
        return !m.isPet && m.role == ROLE_TANK;
    }

    std::vector<PartyMemberFacts const*> Alive(WorldModel const& world, bool botsOnly)
    {
        std::vector<PartyMemberFacts const*> out;
        for (PartyMemberFacts const& m : world.party)
            if (m.alive && IsTank(m) && (!botsOnly || m.isBot))
                out.push_back(&m);
        std::stable_sort(out.begin(), out.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
        {
            if (a->cap.tank != b->cap.tank)
                return a->cap.tank > b->cap.tank;
            return a->guid.GetRawValue() < b->guid.GetRawValue();
        });
        return out;
    }

    PartyMemberFacts const* Main(WorldModel const& world, bool botsOnly)
    {
        std::vector<PartyMemberFacts const*> const tanks = Alive(world, botsOnly);
        return tanks.empty() ? nullptr : tanks.front();
    }

    bool Any(WorldModel const& world)
    {
        if (Main(world))
            return true;
        for (AllyFacts const& a : world.allies)
            if (a.tankLike)
                return true;
        return false;
    }
}
}
