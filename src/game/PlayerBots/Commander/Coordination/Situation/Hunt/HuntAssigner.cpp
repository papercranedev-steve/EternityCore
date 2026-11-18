#include "Commander/Coordination/Situation/Hunt/HuntAssigner.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        uint32 const CRITTER_MASK = 1u << (CREATURE_TYPE_CRITTER - 1);

        uint32 TeamLevel(WorldModel const& world, uint32& members)
        {
            uint32 level = 0;
            members = 0;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet)
                    continue;
                ++members;
                level = std::max(level, m.level);
            }
            return level;
        }

        PackFacts const* FindPackOf(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& p : world.packs)
                for (PackCreature const& c : p.members)
                    if (c.guid == guid)
                        return &p;
            return nullptr;
        }

        bool CanWalkTo(PackFacts const& pack)
        {
            bool const way = pack.route.state == PackRouteState::Walk || pack.route.state == PackRouteState::Straight;
            return way && pack.route.blocker.IsEmpty() && pack.ApproachYd() <= HuntAssigner::MAX_APPROACH_YD;
        }

        // The creature to attack first: the route's target (nearest), else the first member.
        ObjectGuid OpenerOf(PackFacts const& pack)
        {
            if (!pack.route.target.IsEmpty())
                return pack.route.target;
            return pack.members.empty() ? ObjectGuid() : pack.members.front().guid;
        }
    }

    bool HuntAssigner::IsHuntable(WorldModel const& world, PackFacts const& pack)
    {
        uint32 members = 0;
        uint32 const level = TeamLevel(world, members);
        if (!level || pack.members.empty() || pack.members.size() > members)
            return false;
        for (PackCreature const& c : pack.members)
        {
            if (c.elite || c.boss || c.stealthed || c.tappedByOther || (c.creatureTypeMask & CRITTER_MASK))
                return false;
            if (!c.level || c.level > level + LEVEL_ABOVE || c.level + LEVEL_BELOW < level)
                return false;
        }
        return true;
    }

    void HuntAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        HuntPlan& h = plan.hunt;
        if (world.humanInTeam || world.party.empty())
        {
            h = HuntPlan();
            return;
        }
        // The fight is on: its targets are TargetAssigner's.
        if (world.HasPartyEnemy())
        {
            h.target.Clear();
            h.idleSinceMs = 0;
            return;
        }
        if (plan.retreat.active || plan.pull.phase != PullPhase::None)
            return;
        // Hurt, out of mana, someone dead or fighting: rest first (RestBehavior).
        if (!PullPlanner::CheckReady(world).empty())
        {
            h.target.Clear();
            h.idleSinceMs = 0;
            return;
        }

        if (h.Active() && WorldTimer::getMSTimeDiff(h.sinceMs, world.refreshedAtMs) < KEEP_MS)
            if (PackFacts const* pack = FindPackOf(world, h.target))
                if (IsHuntable(world, *pack) && CanWalkTo(*pack))
                    return;

        PackFacts const* best = nullptr;
        for (PackFacts const& pack : world.packs)
            if (IsHuntable(world, pack) && CanWalkTo(pack) && (!best || pack.ApproachYd() < best->ApproachYd()))
                best = &pack;

        if (!best)
        {
            h.target.Clear();
            h.why = "";
            if (!h.idleSinceMs)
                h.idleSinceMs = world.refreshedAtMs;
            return;
        }
        ObjectGuid const target = OpenerOf(*best);
        if (target != h.target)
            h.sinceMs = world.refreshedAtMs;
        h.target = target;
        h.idleSinceMs = 0;
        h.why = "the nearest fight at our level";
    }

    void HuntAssigner::Clear(TeamPlan& plan) const
    {
        plan.hunt = HuntPlan();
    }
}
