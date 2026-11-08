#include "Commander/Perception/SafeRoute.h"
#include "Commander/Perception/AggroRange.h"
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
        // Idle creatures within `range` of `center` that would attack `me` (AggroRange.h).
        class IdleAggressorCheck
        {
        public:
            IdleAggressorCheck(Player const* me, Position3 const& center, float range)
                : m_me(me), m_center(center), m_range(range) {}
            bool operator()(Creature* c)
            {
                return AggroRange::IsIdleAggressor(c, m_me) && c->GetDistance2d(m_center.x, m_center.y) <= m_range;
            }

        private:
            Player const* m_me;
            Position3 m_center;
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

    IdleCreatures::IdleCreatures(Player* me, WorldObject const* center, float margin, float scanRadius)
    {
        if (!center)
            center = me;
        Scan(me, me->GetMap(), PositionOf(center), margin, scanRadius);
    }

    IdleCreatures::IdleCreatures(Player* me, Position3 const& center, float scanRadius, float margin)
    {
        Scan(me, me->GetMap(), center, margin, scanRadius);
    }

    void IdleCreatures::Scan(Player* me, Map* map, Position3 const& center, float margin, float scanRadius)
    {
        std::list<Creature*> found;
        IdleAggressorCheck check(me, center, scanRadius);
        MaNGOS::CreatureListSearcher<IdleAggressorCheck> searcher(found, check);
        Cell::VisitGridObjects(center.x, center.y, map, searcher, scanRadius);
        for (Creature* c : found)
        {
            Idle i;
            i.creature = c;
            i.reach = AggroRange::Reach(c, me, margin);
            i.home = AggroRange::WanderOf(c).home;
            if (i.reach > 0.0f)
                m_idle.push_back(i);
        }
    }

    void IdleCreatures::CountOnlyCloserThan(Position3 const& end)
    {
        for (Idle& i : m_idle)
        {
            if (!AggroRange::CoversWandering(i.creature, i.reach, end))
                continue;
            float const gap = Distance2d(end, i.home);
            i.endGap = i.endGap < 0.0f ? gap : std::min(i.endGap, gap);
        }
    }

    IdleCreatures IdleCreatures::LeavingFrom(Position3 const& from) const
    {
        IdleCreatures out = *this;
        out.CountOnlyCloserThan(from);
        return out;
    }

    IdleCreatures IdleCreatures::ArrivingAt(Position3 const& to) const
    {
        IdleCreatures out = *this;
        out.CountOnlyCloserThan(to);
        return out;
    }

    void IdleCreatures::Ignore(std::vector<ObjectGuid> const& guids)
    {
        m_idle.erase(std::remove_if(m_idle.begin(), m_idle.end(), [&guids](Idle const& i)
        {
            return std::find(guids.begin(), guids.end(), i.creature->GetObjectGuid()) != guids.end();
        }), m_idle.end());
    }

    Creature const* IdleCreatures::Blocker(Position3 const& p) const
    {
        for (Idle const& c : m_idle)
        {
            // Walking out of its circle or into it at a fight: no closer to it than that end.
            if (c.endGap >= 0.0f && Distance2d(p, c.home) >= c.endGap - END_GAP_TOLERANCE)
                continue;
            if (AggroRange::CoversWandering(c.creature, c.reach, p))
                return c.creature;
        }
        return nullptr;
    }

    bool SegmentIsSafe(IdleCreatures const& idle, Position3 const& a, Position3 const& b, Creature const** blocker)
    {
        // Path corners are far apart in open rooms, so walk each segment in PATH_SAMPLE_STEP steps.
        uint32 const steps = std::max(1u, uint32(Distance2d(a, b) / PATH_SAMPLE_STEP));
        for (uint32 s = 1; s <= steps; ++s)
        {
            float const f = float(s) / steps;
            Position3 p;
            p.x = a.x + (b.x - a.x) * f;
            p.y = a.y + (b.y - a.y) * f;
            p.z = a.z + (b.z - a.z) * f;
            if (Creature const* c = idle.Blocker(p))
            {
                if (blocker)
                    *blocker = c;
                return false;
            }
        }
        return true;
    }

    namespace
    {
        bool PathPointsAreSafe(IdleCreatures const& idle, PathInfo const& path, Creature const** blocker = nullptr)
        {
            auto const& pts = path.getPath();
            for (size_t i = 1; i < pts.size(); ++i)
            {
                Position3 a, b;
                a.x = pts[i - 1].x; a.y = pts[i - 1].y; a.z = pts[i - 1].z;
                b.x = pts[i].x;     b.y = pts[i].y;     b.z = pts[i].z;
                if (!SegmentIsSafe(idle, a, b, blocker))
                    return false;
            }
            return true;
        }

        bool Fail(SafeRoute::Verdict* verdict, char const* why)
        {
            if (verdict)
                verdict->why = why;
            return false;
        }
    }

    bool PathReaches(Map const* map, PathInfo const& path, Position3 const& to)
    {
        auto const& pts = path.getPath();
        if (pts.empty())
            return false;
        Position3 end;
        end.x = pts.back().x;
        end.y = pts.back().y;
        end.z = pts.back().z;
        float const gap = Distance2d(end, to);
        if (gap <= PATH_END_TOLERANCE)
            return true;
        return gap <= OFF_MESH_STEP && map->isInLineOfSight(end.x, end.y, end.z + 2.0f, to.x, to.y, to.z + 2.0f);
    }

    bool PathIsSafe(Player* me, IdleCreatures const& idle, Position3 const& to, float maxDetour, float* length,
                    Position3* end, Verdict* verdict)
    {
        if (verdict)
            *verdict = Verdict();
        Position3 const here = PositionOf(me);
        if (end)
            *end = to;
        PathInfo path(me);
        path.calculate(to.x, to.y, to.z);
        uint32 const type = path.getPathType();
        if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
            return Fail(verdict, "no path");
        if (type & PATHFIND_NOT_USING_PATH)
        {
            if (length)
                *length = Distance2d(here, to);
            if (!me->GetMap()->isInLineOfSight(here.x, here.y, here.z + 2.0f, to.x, to.y, to.z + 2.0f))
                return Fail(verdict, "no sight");
            return true;
        }
        if (length)
            *length = path.Length();
        if (end && !path.getPath().empty())
        {
            end->x = path.getPath().back().x;
            end->y = path.getPath().back().y;
            end->z = path.getPath().back().z;
        }
        if (!PathReaches(me->GetMap(), path, to))
            return Fail(verdict, "does not reach");

        if (path.Length() > Distance2d(here, to) * maxDetour + 2.0f)
            return Fail(verdict, "detour");
        Creature const* blocker = nullptr;
        if (!PathPointsAreSafe(idle, path, &blocker))
        {
            if (verdict)
                verdict->blocker = blocker;
            return Fail(verdict, "idle creature");
        }
        return true;
    }

    bool WalkIsSafe(Player* me, IdleCreatures const& idle, Position3 const& to)
    {
        PathInfo path(me);
        path.calculate(to.x, to.y, to.z);
        if (path.getPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH))
            return SegmentIsSafe(idle, PositionOf(me), to);
        return PathPointsAreSafe(idle, path);
    }
}
}
