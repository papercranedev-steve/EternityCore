#include "Commander/Perception/Spatial/AmbushSpot.h"
#include "Commander/Perception/Spatial/AggroRange.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace AmbushSpot
{
    namespace
    {
        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        float Distance3d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // Another counted patrol comes to `at` within `beforeSec` (not the ambushed one or its group).
        bool OtherPatrolComes(WorldModel const& world, Request const& r, Position3 const& at, float beforeSec)
        {
            for (PatrolFacts const& p : world.patrols)
            {
                if (p.guid == r.facts->guid ||
                    std::find(r.ignore.begin(), r.ignore.end(), p.guid) != r.ignore.end())
                    continue;
                if (PatrolZone::Comes(world, p, at, beforeSec))
                    return true;
            }
            return false;
        }
    }

    bool Find(WorldModel const& world, Map* map, Request const& r, Spot& out, Failure* why)
    {
        Failure failure = Failure::None;
        auto reached = [&failure](Failure stage) { failure = std::max(failure, stage); };
        auto fail = [&why, &failure]()
        {
            if (why)
                *why = failure;
            return false;
        };
        if (why)
            *why = Failure::None;
        if (!map || !r.patrol || !r.facts || !r.tank || !PatrolZone::Counts(*r.facts))
        {
            reached(Failure::NoTrack);
            return fail();
        }

        PatrolFacts const& p = *r.facts;
        Position3 const tankAt = PositionOf(r.tank);
        // The tank stands this close to the patrol's way: it notices the tank.
        float const notice = std::max(1.0f, r.patrol->GetAttackDistance(r.tank) - AGGRO_INSIDE);
        float const age = PatrolZone::TrackAgeSec(world);

        float const scan = std::min(SafeRoute::SCAN_RADIUS + 30.0f,
            PackResolver::Distance(tankAt, r.near) + r.maxFromNearYd + AggroRange::MAX_ATTACK_DISTANCE);
        SafeRoute::IdleCreatures idle(r.tank, tankAt, scan);
        std::vector<ObjectGuid> ignore = r.ignore;
        ignore.push_back(p.guid);
        idle.Ignore(ignore);

        uint32 paths = 0;
        bool checkedAny = false;
        Position3 lastChecked;
        for (size_t i = 0; i < p.track.size(); ++i)
        {
            float const sec = PatrolZone::SecOf(p, i, age);
            if (sec > r.maxWaitSec)
                break;
            Position3 const point = p.track[i];
            if (checkedAny && PackResolver::Distance(point, lastChecked) < MIN_STEP_YD)
                continue;
            checkedAny = true;
            lastChecked = point;

            if (PackResolver::Distance(point, r.near) > r.maxFromNearYd)
            {
                reached(Failure::TooFar);
                continue;
            }
            float const ground = map->GetHeight(point.x, point.y, point.z + 2.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(point.z - ground) > notice)
            {
                reached(Failure::NoGround);
                continue;
            }
            Position3 stand = point;
            stand.z = ground;

            // The first time the patrol is near enough after the tank got there.
            auto meet = [&](float readySec, Position3& meetAt) -> float
            {
                for (size_t j = 0; j < p.track.size(); ++j)
                {
                    float const at = PatrolZone::SecOf(p, j, age);
                    if (at > r.maxWaitSec)
                        break;
                    if (at >= readySec && Distance3d(p.track[j], stand) <= notice)
                    {
                        meetAt = p.track[j];
                        return at;
                    }
                }
                return -1.0f;
            };
            float const walkSec = PackResolver::Distance(tankAt, stand) * PATH_FACTOR / RUN_YD_PER_SEC + LEAD_SEC;
            Position3 meetAt;
            float arrive = meet(walkSec, meetAt);
            if (arrive < 0.0f)
            {
                reached(Failure::Late);
                continue;
            }

            if (idle.Near(stand))
            {
                reached(Failure::Aggro);
                continue;
            }
            if (OtherPatrolComes(world, r, stand, arrive + LEAD_SEC))
            {
                reached(Failure::Patrol);
                continue;
            }
            if (!map->isInLineOfSight(meetAt.x, meetAt.y, meetAt.z + UNIT_HEIGHT, stand.x, stand.y, stand.z + UNIT_HEIGHT))
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
            float length = 0.0f;
            if (!SafeRoute::PathIsSafe(r.tank, idle, stand, MAX_DETOUR, &length))
            {
                reached(Failure::NoPath);
                continue;
            }
            // The real walk may be longer than the straight line allowed for.
            float const pathSec = length / RUN_YD_PER_SEC + LEAD_SEC;
            if (pathSec > walkSec)
            {
                arrive = meet(pathSec, meetAt);
                if (arrive < 0.0f)
                {
                    reached(Failure::Late);
                    continue;
                }
            }

            out.at = stand;
            out.meetAt = meetAt;
            out.arriveSec = arrive;
            out.walkYd = length;
            if (why)
                *why = Failure::None;
            return true;
        }
        if (!checkedAny)
            reached(Failure::TooFar);
        return fail();
    }

    char const* FailureName(Failure failure)
    {
        switch (failure)
        {
            case Failure::None:     return "none";
            case Failure::NoTrack:  return "its way is not known";
            case Failure::TooFar:   return "its way stays away from the party";
            case Failure::NoGround: return "no ground below its way";
            case Failure::Late:     return "it passes before the tank could be there";
            case Failure::Aggro:    return "its way runs next to other creatures";
            case Failure::Patrol:   return "another patrol comes there first";
            case Failure::NoSight:  return "it would not see the tank";
            case Failure::NoPath:   return "no safe walk to its way";
            case Failure::Budget:   return "search budget used up";
        }
        return "?";
    }
}
}
