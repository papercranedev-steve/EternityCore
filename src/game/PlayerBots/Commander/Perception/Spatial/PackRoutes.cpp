#include "Commander/Perception/Spatial/PackRoutes.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Creature.h"
#include "PathFinder.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
namespace PackRoutes
{
    namespace
    {
        Position3 ToPosition(Vector3 const& v)
        {
            Position3 p;
            p.x = v.x;
            p.y = v.y;
            p.z = v.z;
            return p;
        }

        // The member the walk goes to: nearest to the leader (the pull starts on that side).
        PackCreature const* NearestMember(PackFacts const& pack, Position3 const& from)
        {
            PackCreature const* best = nullptr;
            float bestYd = 0.0f;
            for (PackCreature const& c : pack.members)
            {
                float const yd = PackResolver::Distance(c.pos, from);
                if (!best || yd < bestYd)
                {
                    best = &c;
                    bestYd = yd;
                }
            }
            return best;
        }

        std::vector<ObjectGuid> GuidsOf(PackFacts const& pack)
        {
            std::vector<ObjectGuid> guids;
            guids.reserve(pack.members.size());
            for (PackCreature const& c : pack.members)
                guids.push_back(c.guid);
            return guids;
        }

        // The first creature of `idle` the path comes into, or null.
        Creature const* FirstBlocker(SafeRoute::IdleCreatures const& idle, PathInfo const& path)
        {
            auto const& pts = path.getPath();
            for (size_t i = 1; i < pts.size(); ++i)
            {
                Creature const* blocker = nullptr;
                if (!SafeRoute::SegmentIsSafe(idle, ToPosition(pts[i - 1]), ToPosition(pts[i]), &blocker))
                    return blocker;
            }
            return nullptr;
        }

        // The leader has line of sight to a member (the nearest first).
        bool LeaderSees(Player* leader, PackFacts const& pack, PackCreature const& nearest)
        {
            auto sees = [leader](PackCreature const& c) { return leader->IsWithinLOS(c.pos.x, c.pos.y, c.pos.z); };
            if (sees(nearest))
                return true;
            return std::any_of(pack.members.begin(), pack.members.end(), [&](PackCreature const& c)
            {
                return &c != &nearest && sees(c);
            });
        }

        void Route(Player* leader, Position3 const& from, SafeRoute::IdleCreatures const& allIdle, PackFacts& pack)
        {
            PackCreature const* target = NearestMember(pack, from);
            if (!target)
                return;
            pack.route.target = target->guid;
            pack.route.leaderSees = LeaderSees(leader, pack, *target);

            PathInfo path(leader);
            path.calculate(target->pos.x, target->pos.y, target->pos.z);
            uint32 const type = path.getPathType();
            if (type & PATHFIND_NOT_USING_PATH)
            {
                // No navmesh (or tiles not loaded): only the straight line is known.
                pack.route.state = PackRouteState::Straight;
                pack.route.walkYd = PackResolver::Distance(from, target->pos);
                return;
            }
            // A path ending off the mesh near the creature (a ledge) still counts: the creature
            // comes down to the party once pulled; the pull planner judges the pull itself.
            if ((type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)) || path.getPath().empty())
            {
                pack.route.state = PackRouteState::NoWay;
                return;
            }
            pack.route.state = PackRouteState::Walk;
            pack.route.walkYd = path.Length();

            // The pack itself comes anyway: only other creatures block the walk.
            SafeRoute::IdleCreatures idle = allIdle;
            idle.Ignore(GuidsOf(pack));
            if (Creature const* blocker = FirstBlocker(idle, path))
                pack.route.blocker = blocker->GetObjectGuid();
        }
    }

    void Fill(Player* leader, std::vector<PackFacts>& packs)
    {
        if (packs.empty())
            return;
        Position3 from;
        from.x = leader->GetPositionX();
        from.y = leader->GetPositionY();
        from.z = leader->GetPositionZ();

        size_t const count = std::min<size_t>(packs.size(), MAX_ROUTED_PACKS);
        float farthest = 0.0f;
        for (size_t i = 0; i < count; ++i)
            farthest = std::max(farthest, packs[i].distanceToLeader);

        // One scan for all walks. The hard line (TIGHT_MARGIN): "blocked" means the walk really
        // comes into a creature's aggro range, not only near the comfort margin.
        SafeRoute::IdleCreatures const idle(leader, from, farthest + IDLE_SCAN_MARGIN, SafeRoute::TIGHT_MARGIN);
        for (size_t i = 0; i < count; ++i)
            Route(leader, from, idle, packs[i]);
    }
}
}
