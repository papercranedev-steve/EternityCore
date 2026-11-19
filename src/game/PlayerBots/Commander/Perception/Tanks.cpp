#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Spatial/PackResolver.h"
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

    bool CloserBot(WorldModel const& world, ObjectGuid self, Position3 const& pos, float distance, float margin)
    {
        for (PartyMemberFacts const* t : Alive(world, true))
            if (t->guid != self && t->CanAct() && PackResolver::Distance(t->pos, pos) < distance - margin)
                return true;
        return false;
    }

    bool IsMeleeFront(WorldModel const& world, PartyMemberFacts const& m)
    {
        if (!m.isPet)
            return IsTank(m);
        if (m.tauntsItself || m.canOffTank)
            return true;
        PartyMemberFacts const* owner = world.FindMember(m.ownerGuid);
        return owner && owner->classId == CLASS_HUNTER;
    }

    PartyMemberFacts const* HitTaker(WorldModel const& world)
    {
        if (PartyMemberFacts const* tank = Main(world))
            return tank;
        for (PartyMemberFacts const& m : world.party)
            if (m.functions & FUNCTION_HOLD)
                return nullptr;     // its tank is dead: no stand-in guessed here
        PartyMemberFacts const* best = nullptr;
        uint32 bestHits = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive)
                continue;
            uint32 hits = 0;
            for (EnemyFacts const& e : world.enemies)
                if (!e.onAllyOnly && e.acting && e.victim == m.guid)
                    ++hits;
            // (a pet only when enemies hit it: a hunter's pet holding)
            if (!hits && m.isPet)
                continue;
            if (!best || hits > bestHits || (hits == bestHits && m.maxHealth > best->maxHealth))
            {
                best = &m;
                bestHits = hits;
            }
        }
        return best;
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
