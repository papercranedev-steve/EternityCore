#include "Commander/Coordination/Situation/Hunt/HuntAssigner.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/FightLoad.h"
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

        // The fight forecast (FightLoad, every creature of the pack on Tanks::HitTaker: the
        // tank, or with nobody to hold the member hit; the team's damage and its healers by
        // function) ends with the team standing. No one to put it on: not forecast, the rules
        // of IsHuntable only.
        bool Winnable(WorldModel const& world, PackFacts const& pack)
        {
            FightLoad::Input in;
            PartyMemberFacts const* victim = Tanks::HitTaker(world);
            if (!victim || !FightLoad::FromWorld(world, in))
                return true;
            for (PackCreature const& c : pack.members)
                in.foes.push_back(FightLoad::FoeOf(c, *victim, 0.0f));
            return FightLoad::Bearable(FightLoad::Simulate(in, "hunt"));
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
            // A player is about to fight it (nobody hit it yet): his.
            if (!c.claimedBy.IsEmpty())
                return false;
            if (!c.level || c.level > level + LEVEL_ABOVE || c.level + LEVEL_BELOW < level)
                return false;
        }
        return true;
    }

    void HuntAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        HuntPlan& h = plan.hunt;
        if (world.humanInTeam || world.party.empty() || !world.seeksFights)
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

        // On our way to it and a player claimed it first: left to him (a new choice below).
        if (h.Active())
            if (PackFacts const* pack = FindPackOf(world, h.target))
                for (PackCreature const& c : pack->members)
                    if (!c.claimedBy.IsEmpty())
                    {
                        h.yieldedTo = c.claimedBy;
                        h.yieldedAtMs = world.refreshedAtMs ? world.refreshedAtMs : 1;
                        h.target.Clear();
                        break;
                    }

        if (h.Active() && WorldTimer::getMSTimeDiff(h.sinceMs, world.refreshedAtMs) < KEEP_MS)
            if (PackFacts const* pack = FindPackOf(world, h.target))
                if (IsHuntable(world, *pack) && CanWalkTo(*pack))
                    return;

        // The nearest one that passes the rules and the forecast (the forecast only for the
        // candidates, nearest first, until one passes).
        std::vector<PackFacts const*> candidates;
        for (PackFacts const& pack : world.packs)
            if (IsHuntable(world, pack) && CanWalkTo(pack))
                candidates.push_back(&pack);
        std::sort(candidates.begin(), candidates.end(),
            [](PackFacts const* a, PackFacts const* b) { return a->ApproachYd() < b->ApproachYd(); });
        // A member's preferred creature (acting out a quest): its packs first, nearest first.
        if (world.preferredPrey)
            std::stable_partition(candidates.begin(), candidates.end(), [&world](PackFacts const* p)
            {
                return std::any_of(p->members.begin(), p->members.end(),
                    [&world](PackCreature const& c) { return c.entry == world.preferredPrey; });
            });
        PackFacts const* best = nullptr;
        for (PackFacts const* pack : candidates)
            if (Winnable(world, *pack))
            {
                best = pack;
                break;
            }

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
        h.why = world.preferredPrey && std::any_of(best->members.begin(), best->members.end(),
                    [&world](PackCreature const& c) { return c.entry == world.preferredPrey; })
            ? "the nearest fight with our quest's creature"
            : "the nearest fight at our level we can win";
    }

    void HuntAssigner::Clear(TeamPlan& plan) const
    {
        plan.hunt = HuntPlan();
    }
}
