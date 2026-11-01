#include "Commander/Behaviors/FleePoint.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <vector>

namespace Commander
{
    namespace
    {
        struct Point
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        float Dist2(Point const& a, Point const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        Point PointOf(WorldObject const* obj)
        {
            Point p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

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

        struct Idle
        {
            Point pos;
            float clearance = 0.0f;
        };

        bool NearIdle(Point const& p, std::vector<Idle> const& idle)
        {
            for (Idle const& c : idle)
                if (Dist2(p, c.pos) <= c.clearance && std::fabs(p.z - c.pos.z) < 10.0f)
                    return true;
            return false;
        }

        struct Candidate
        {
            Point pos;
            float score = 0.0f;
        };

        bool IsTank(Player* member)
        {
            if (PartyBotAI* ai = member->AI() ? dynamic_cast<PartyBotAI*>(member->AI()) : nullptr)
                return ai->GetRole() == ROLE_TANK;
            return IsTankingForm(member->GetShapeshiftForm()) ||
                   (member->GetClass() == CLASS_WARRIOR && member->GetShapeshiftForm() == FORM_DEFENSIVESTANCE);
        }
    }

namespace FleePoint
{
    bool Find(Player* me, Unit* enemy, float& outX, float& outY, float& outZ)
    {
        Map* map = me->GetMap();
        Point const here = PointOf(me);
        Point const foe = PointOf(enemy);
        float const nowGap = Dist2(here, foe);

        std::vector<Idle> idle;
        {
            std::list<Creature*> found;
            IdleHostileCheck check(me, SCAN_RADIUS);
            MaNGOS::CreatureListSearcher<IdleHostileCheck> searcher(found, check);
            Cell::VisitGridObjects(me, searcher, SCAN_RADIUS);
            for (Creature* c : found)
                idle.push_back({ PointOf(c), std::max(MIN_IDLE_CLEARANCE, c->GetAttackDistance(me) + IDLE_MARGIN) });
        }

        // Party members to run to, and the centre of the fighting party.
        std::vector<std::pair<Point, bool>> allies;     // position, is a tank
        Point centre;
        uint32 counted = 0;
        if (Group* group = me->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* m = itr->getSource();
                if (!m || m == me || !m->IsInWorld() || m->GetMap() != map || !m->IsAlive())
                    continue;
                Point const p = PointOf(m);
                if (Dist2(here, p) > MAX_ALLY_DISTANCE)
                    continue;
                centre.x += p.x;
                centre.y += p.y;
                ++counted;
                allies.emplace_back(p, IsTank(m));
            }
        }
        if (counted)
        {
            centre.x /= counted;
            centre.y /= counted;
        }
        else
            centre = here;

        std::vector<Candidate> candidates;
        auto consider = [&](Point p, bool toTank)
        {
            float const ground = map->GetHeight(p.x, p.y, here.z + MAX_HEIGHT_DIFFERENCE, true, 2.0f * MAX_HEIGHT_DIFFERENCE);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - here.z) > MAX_HEIGHT_DIFFERENCE)
                return;
            p.z = ground;
            float const gap = Dist2(p, foe);
            if (!toTank && (gap < MIN_ENEMY_GAP || gap < nowGap + MIN_GAIN))
                return;
            if (NearIdle(p, idle))
                return;
            Candidate c;
            c.pos = p;
            c.score = std::min(gap, ENEMY_DISTANCE_CAP) - CENTRE_WEIGHT * Dist2(p, centre) + (toTank ? TANK_BONUS : 0.0f);
            candidates.push_back(c);
        };

        for (auto const& ally : allies)
            consider(ally.first, ally.second);
        for (float distance : RING_DISTANCES)
        {
            for (uint32 i = 0; i < RING_ANGLES; ++i)
            {
                float const angle = 2.0f * float(M_PI) * i / RING_ANGLES;
                Point p;
                p.x = here.x + std::cos(angle) * distance;
                p.y = here.y + std::sin(angle) * distance;
                consider(p, false);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](Candidate const& a, Candidate const& b) { return a.score > b.score; });

        uint32 checks = 0;
        for (Candidate const& c : candidates)
        {
            if (checks++ >= MAX_PATH_CHECKS)
                break;
            PathInfo path(me);
            path.calculate(c.pos.x, c.pos.y, c.pos.z);
            uint32 const type = path.getPathType();
            if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
                continue;
            bool ok = true;
            if (type & PATHFIND_NOT_USING_PATH)
                ok = map->isInLineOfSight(here.x, here.y, here.z + 2.0f, c.pos.x, c.pos.y, c.pos.z + 2.0f);
            else
            {
                if (path.Length() > Dist2(here, c.pos) * MAX_PATH_DETOUR + 2.0f)
                    ok = false;
                // The way there must not pass an idle pack either: path corners are far apart in
                // open rooms, so walk each segment in PATH_SAMPLE_STEP steps.
                auto const& pts = path.getPath();
                for (size_t i = 1; ok && i < pts.size(); ++i)
                {
                    Point a, b;
                    a.x = pts[i - 1].x; a.y = pts[i - 1].y; a.z = pts[i - 1].z;
                    b.x = pts[i].x;     b.y = pts[i].y;     b.z = pts[i].z;
                    uint32 const steps = std::max(1u, uint32(Dist2(a, b) / PATH_SAMPLE_STEP));
                    for (uint32 s = 1; s <= steps; ++s)
                    {
                        float const f = float(s) / steps;
                        Point p;
                        p.x = a.x + (b.x - a.x) * f;
                        p.y = a.y + (b.y - a.y) * f;
                        p.z = a.z + (b.z - a.z) * f;
                        if (NearIdle(p, idle))
                        {
                            ok = false;
                            break;
                        }
                    }
                }
            }
            if (!ok)
                continue;
            outX = c.pos.x;
            outY = c.pos.y;
            outZ = c.pos.z;
            return true;
        }
        return false;
    }
}
}
