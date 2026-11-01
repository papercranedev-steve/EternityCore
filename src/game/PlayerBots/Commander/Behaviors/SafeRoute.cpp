#include "Commander/Behaviors/SafeRoute.h"
#include "Creature.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>

namespace Commander
{
namespace SafeRoute
{
    namespace
    {
        // Same filter as RetreatPlanner's idle hostiles: alive, not fighting, would attack us.
        class IdleHostileCheck
        {
        public:
            IdleHostileCheck(Player const* me, float range) : m_me(me), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && !c->IsInCombat() && !c->GetOwnerGuid() &&
                    c->GetCreatureType() != CREATURE_TYPE_CRITTER &&
                    !c->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO) &&
                    c->IsHostileTo(m_me) && m_me->IsWithinDist(c, m_range, false);
            }

        private:
            Player const* m_me;
            float m_range;
        };

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }
    }

    float Distance2d(Position3 const& a, Position3 const& b)
    {
        float const dx = a.x - b.x, dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    IdleCreatures::IdleCreatures(Player* me, float scanRadius)
    {
        std::list<Creature*> found;
        IdleHostileCheck check(me, scanRadius);
        MaNGOS::CreatureListSearcher<IdleHostileCheck> searcher(found, check);
        Cell::VisitGridObjects(me, searcher, scanRadius);
        for (Creature* c : found)
        {
            Idle i;
            i.pos = PositionOf(c);
            i.clearance = std::max(MIN_IDLE_CLEARANCE, c->GetAttackDistance(me) + IDLE_MARGIN);
            m_idle.push_back(i);
        }
    }

    bool IdleCreatures::Near(Position3 const& p) const
    {
        for (Idle const& c : m_idle)
            if (Distance2d(p, c.pos) <= c.clearance && std::fabs(p.z - c.pos.z) < IDLE_MAX_HEIGHT_DIFF)
                return true;
        return false;
    }

    bool PathIsSafe(Player* me, IdleCreatures const& idle, Position3 const& to, float maxDetour)
    {
        Position3 const here = PositionOf(me);
        PathInfo path(me);
        path.calculate(to.x, to.y, to.z);
        uint32 const type = path.getPathType();
        if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
            return false;
        if (type & PATHFIND_NOT_USING_PATH)
            return me->GetMap()->isInLineOfSight(here.x, here.y, here.z + 2.0f, to.x, to.y, to.z + 2.0f);

        if (path.Length() > Distance2d(here, to) * maxDetour + 2.0f)
            return false;
        // Path corners are far apart in open rooms, so walk each segment in PATH_SAMPLE_STEP steps.
        auto const& pts = path.getPath();
        for (size_t i = 1; i < pts.size(); ++i)
        {
            Position3 a, b;
            a.x = pts[i - 1].x; a.y = pts[i - 1].y; a.z = pts[i - 1].z;
            b.x = pts[i].x;     b.y = pts[i].y;     b.z = pts[i].z;
            uint32 const steps = std::max(1u, uint32(Distance2d(a, b) / PATH_SAMPLE_STEP));
            for (uint32 s = 1; s <= steps; ++s)
            {
                float const f = float(s) / steps;
                Position3 p;
                p.x = a.x + (b.x - a.x) * f;
                p.y = a.y + (b.y - a.y) * f;
                p.z = a.z + (b.z - a.z) * f;
                if (idle.Near(p))
                    return false;
            }
        }
        return true;
    }
}
}
