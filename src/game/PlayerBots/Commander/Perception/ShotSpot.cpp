#include "Commander/Perception/ShotSpot.h"
#include "Commander/Perception/AggroRange.h"
// SafeRoute (walk checks against idle creatures) lives with the behaviours that use it most;
// it only reads facts, so the eye uses it too.
#include "Commander/Behaviors/SafeRoute.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>

namespace Commander
{
namespace ShotSpot
{
    namespace
    {
        class IdleCheck
        {
        public:
            explicit IdleCheck(Unit const* who) : m_who(who) {}
            bool operator()(Creature* c) { return AggroRange::IsIdleAggressor(c, m_who); }

        private:
            Unit const* m_who;
        };

        // An idle creature as the spot check sees it: where it is, where it wanders around.
        struct Idle
        {
            Position3 pos;
            Position3 home;
            float reach = 0.0f;     // aggro range + AGGRO_MARGIN
            float wander = 0.0f;    // random movement radius around home, 0 = none
            bool flies = false;
        };

        float Distance3d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        std::vector<Idle> ScanIdle(Creature const* center, Player* who, float radius)
        {
            std::list<Creature*> found;
            IdleCheck check(who);
            MaNGOS::CreatureListSearcher<IdleCheck> searcher(found, check);
            Cell::VisitGridObjects(center, searcher, radius);

            std::vector<Idle> out;
            for (Creature* c : found)
            {
                Idle i;
                i.reach = AggroRange::Reach(c, who, AGGRO_MARGIN);
                if (i.reach <= 0.0f)
                    continue;
                i.pos = PositionOf(c);
                i.home = i.pos;
                if (c->GetDefaultMovementType() == RANDOM_MOTION_TYPE && c->GetWanderDistance() > 0.0f)
                {
                    Position const& h = c->GetHomePosition();
                    i.home.x = h.x;
                    i.home.y = h.y;
                    i.home.z = h.z;
                    i.wander = c->GetWanderDistance();
                }
                i.flies = c->CanFly();
                out.push_back(i);
            }
            return out;
        }

        // Smallest (distance - reach) over the creatures; < 0 = some creature reaches `at`.
        float Clearance(std::vector<Idle> const& idle, Position3 const& at)
        {
            float clearance = NO_CREATURE_CLEARANCE;
            for (Idle const& c : idle)
            {
                if (!c.flies && std::fabs(c.pos.z - at.z) > CREATURE_Z_ATTACK_RANGE &&
                    std::fabs(c.home.z - at.z) > CREATURE_Z_ATTACK_RANGE)
                    continue;
                clearance = std::min(clearance, Distance3d(c.pos, at) - c.reach);
                if (c.wander > 0.0f)
                    clearance = std::min(clearance, Distance3d(c.home, at) - c.reach - c.wander);
            }
            return clearance;
        }

        float ScanRadius(float maxRange)
        {
            // Candidates lie within maxRange (+ both combat reaches) of the target; a creature
            // can reach a candidate from at most its aggro range away.
            return maxRange + 5.0f + AggroRange::MAX_ATTACK_DISTANCE;
        }
    }

    bool Find(Map* map, Request const& request, Spot& out, Failure* why)
    {
        // The furthest check any candidate got to (enum order) says why none was found.
        Failure failure = Failure::None;
        auto reached = [&failure](Failure stage) { failure = std::max(failure, stage); };
        auto fail = [&why, &failure]()
        {
            if (why)
                *why = failure;
            return false;
        };
        Creature const* target = request.target;
        Player* shooter = request.shooter;
        if (!map || !target || !shooter || request.maxRange <= 0.0f)
            return fail();

        Position3 const t = PositionOf(target);
        Position3 const s = PositionOf(shooter);
        float const reaches = target->GetCombatReach() + shooter->GetCombatReach();
        float const base = std::atan2(s.y - t.y, s.x - t.x);
        float const closest = request.minRange > 0.0f ? request.minRange + MIN_RANGE_EXTRA : 0.0f;

        std::vector<Idle> const idle = ScanIdle(target, shooter, ScanRadius(request.maxRange));
        // The walk check (SafeRoute) uses the server's rule (line of sight included) around the target.
        SafeRoute::IdleCreatures const walkIdle(shooter, target, AGGRO_MARGIN, ScanRadius(request.maxRange));

        uint32 losChecks = 0;
        uint32 paths = 0;
        for (float const offset : RING_OFFSETS)
        {
            float const range = request.maxRange - offset;
            if (range < closest || range <= 0.0f)
                continue;
            float const radius = range + reaches;
            for (float const turn : TURNS)
            {
                Position3 p;
                p.x = t.x + std::cos(base + turn) * radius;
                p.y = t.y + std::sin(base + turn) * radius;
                float const ground = map->GetHeight(p.x, p.y, t.z + 2.0f);
                if (ground <= INVALID_HEIGHT || std::fabs(ground - t.z) > MAX_HEIGHT_DIFF)
                {
                    reached(Failure::NoGround);
                    continue;
                }
                p.z = ground;

                if (std::any_of(request.avoid.begin(), request.avoid.end(),
                    [&p](Position3 const& a) { return SafeRoute::Distance2d(a, p) < AVOID_RADIUS; }))
                    continue;

                float const clearance = Clearance(idle, p);
                if (clearance < 0.0f)
                {
                    reached(Failure::Aggro);
                    continue;
                }

                if (losChecks >= MAX_LOS_CHECKS)
                {
                    reached(Failure::Budget);
                    return fail();
                }
                ++losChecks;
                if (!target->IsWithinLOS(p.x, p.y, p.z, true, UNIT_HEIGHT))
                {
                    reached(Failure::NoSight);
                    continue;
                }

                if (paths >= MAX_PATHS)
                {
                    reached(Failure::Budget);
                    return fail();
                }
                ++paths;
                if (!SafeRoute::PathIsSafe(shooter, walkIdle, p, MAX_DETOUR))
                {
                    reached(Failure::NoPath);
                    continue;
                }

                out.at = p;
                out.range = range;
                out.clearance = clearance;
                if (why)
                    *why = Failure::None;
                return true;
            }
        }
        return fail();
    }

    bool IsClear(Creature const* target, Player* shooter, Position3 const& at)
    {
        if (!target || !shooter)
            return false;
        float const radius = SafeRoute::Distance2d(PositionOf(target), at) + AggroRange::MAX_ATTACK_DISTANCE;
        return Clearance(ScanIdle(target, shooter, radius), at) >= 0.0f;
    }

    char const* FailureName(Failure failure)
    {
        switch (failure)
        {
            case Failure::None:     return "no candidate";
            case Failure::NoGround: return "no ground nearby";
            case Failure::Aggro:    return "every spot is in a creature's aggro range";
            case Failure::NoSight:  return "no spot outside aggro range can see the target";
            case Failure::NoPath:   return "no safe walk to a spot";
            case Failure::Budget:   return "check budget used up";
        }
        return "?";
    }
}
}
