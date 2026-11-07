#include "Commander/Perception/SapSpot.h"
#include "Commander/Perception/AggroRange.h"
#include "Commander/Perception/PatrolTracker.h"
#include "Commander/Perception/SafeRoute.h"
#include "Commander/Perception/StealthDetect.h"
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
namespace SapSpot
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

        class PatrolCheck
        {
        public:
            explicit PatrolCheck(Unit const* who) : m_who(who) {}
            bool operator()(Creature* c)
            {
                return c->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE && AggroRange::IsIdleAggressor(c, m_who);
            }

        private:
            Unit const* m_who;
        };

        struct Walker
        {
            PatrolTracker::Track track;
            float reach = 0.0f;
        };

        std::vector<Walker> ScanWalkers(Creature* target, Player* rogue, float standSec)
        {
            std::vector<Walker> out;
            if (standSec <= 0.0f)
                return out;
            std::list<Creature*> found;
            PatrolCheck check(rogue);
            MaNGOS::CreatureListSearcher<PatrolCheck> searcher(found, check);
            Cell::VisitGridObjects(target, searcher, SCAN_RADIUS);
            for (Creature* c : found)
            {
                if (c == target)
                    continue;
                Walker w;
                w.reach = AggroRange::Reach(c, rogue, AGGRO_MARGIN);
                if (w.reach <= 0.0f)
                    continue;
                w.track = PatrolTracker::Predict(c, standSec);
                out.push_back(std::move(w));
            }
            return out;
        }
    }

    bool Find(Map* map, Request const& request, Spot& out, Failure* why)
    {
        Failure failure = Failure::None;
        auto reached = [&failure](Failure stage) { failure = std::max(failure, stage); };
        auto done = [&why, &failure](bool found)
        {
            if (why)
                *why = found ? Failure::None : failure;
            return found;
        };
        Creature* target = request.target;
        Player* rogue = request.rogue;
        if (!map || !target || !rogue)
            return done(false);

        Position3 const t = PositionOf(target);
        float const radius = std::max(MIN_RING_YD,
            target->GetCombatReach() + rogue->GetCombatReach() + MELEE_EXTRA_YD - RING_INSIDE_YD);
        float const back = target->GetOrientation() + float(M_PI);

        StealthDetect::Watchers const watchers(rogue, t, SCAN_RADIUS, request.stealthSkill);
        // After the Sap: everyone but the target (stunned) with the normal aggro rule.
        SafeRoute::IdleCreatures seen(rogue, target, AGGRO_MARGIN, SCAN_RADIUS);
        seen.Ignore({ target->GetObjectGuid() });
        std::vector<Walker> const walkers = ScanWalkers(target, rogue, request.standSec);

        uint32 paths = 0;
        for (float const turn : TURNS)
        {
            Position3 p;
            p.x = t.x + std::cos(back + turn) * radius;
            p.y = t.y + std::sin(back + turn) * radius;
            float const ground = map->GetHeight(p.x, p.y, t.z + 2.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - t.z) > MAX_HEIGHT_DIFF)
            {
                reached(Failure::NoGround);
                continue;
            }
            p.z = ground;
            if (watchers.Notice(p))
            {
                reached(Failure::Noticed);
                continue;
            }
            if (seen.Near(p))
            {
                reached(Failure::Seen);
                continue;
            }
            if (std::any_of(walkers.begin(), walkers.end(), [&p](Walker const& w)
                { return PatrolTracker::ClosestApproach(w.track, p, w.reach).comesWithin; }))
            {
                reached(Failure::Patrol);
                continue;
            }
            if (!target->IsWithinLOS(p.x, p.y, p.z, true, AggroRange::LOS_TARGET_HEIGHT))
            {
                reached(Failure::NoSight);
                continue;
            }
            if (paths >= MAX_PATHS)
            {
                reached(Failure::Budget);
                return done(false);
            }
            ++paths;
            float length = 0.0f;
            if (!watchers.WalkIsSafe(rogue, p, MAX_DETOUR, &length))
            {
                reached(Failure::NoPath);
                continue;
            }
            out.at = p;
            out.walkYd = length;
            return done(true);
        }
        return done(false);
    }

    char const* FailureName(Failure failure)
    {
        switch (failure)
        {
            case Failure::None:     return "no candidate";
            case Failure::NoGround: return "no ground next to it";
            case Failure::Noticed:  return "a creature would notice the stealthed rogue at every spot";
            case Failure::Seen:     return "another creature would see the rogue once the Sap breaks stealth";
            case Failure::Patrol:   return "a patrol passes the spots";
            case Failure::NoSight:  return "no spot in sight of it";
            case Failure::NoPath:   return "no unnoticed walk to a spot";
            case Failure::Budget:   return "check budget used up";
        }
        return "?";
    }
}
}
