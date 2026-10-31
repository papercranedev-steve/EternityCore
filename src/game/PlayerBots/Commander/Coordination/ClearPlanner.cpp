#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace ClearPlanner
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

        bool IsSkipped(ClearPlan const& clear, PackFacts const& pack)
        {
            for (PackCreature const& c : pack.members)
                if (std::find(clear.skipped.begin(), clear.skipped.end(), c.guid) != clear.skipped.end())
                    return true;
            return false;
        }

        void Skip(ClearPlan& clear, PackFacts const& pack)
        {
            for (PackCreature const& c : pack.members)
                clear.skipped.push_back(c.guid);
            ++clear.skippedPacks;
        }

        void Note(TeamPlan& plan, std::string text)
        {
            plan.notes.push_back(std::move(text));
        }
    }

    void Start(Player* leader, PartyCommander& commander, float radius, uint32 maxMobs)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        plan.clear = ClearPlan();
        plan.clear.active = true;
        plan.clear.anchor = PositionOf(leader);
        plan.clear.radius = radius;
        plan.clear.maxMobs = maxMobs;
        plan.clear.idleSinceMs = 0;     // first pull right away
        plan.lastPullOutcome = PullOutcome::None;
    }

    void Stop(PartyCommander& commander, std::string const& why)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        if (!plan.clear.active)
            return;
        char buf[200];
        snprintf(buf, sizeof(buf), "Clear stopped after %u packs: %s", plan.clear.cleared, why.c_str());
        plan.clear.active = false;
        Note(plan, buf);
    }

    void Step(PartyBotAI* bot, PartyCommander& commander, uint32 nowMs)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        ClearPlan& clear = plan.clear;
        if (!clear.active)
            return;

        WorldModel const& world = commander.GetWorld();
        Player* leader = bot->GetPartyLeader();
        if (!leader || !leader->IsInWorld() || leader->GetMap() != bot->me->GetMap())
        {
            Stop(commander, "you left the map.");
            return;
        }
        if (PackResolver::Distance(PositionOf(leader), clear.anchor) > MAX_LEADER_DRIFT)
        {
            Stop(commander, "you moved away from the starting point.");
            return;
        }
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isPet && !m.alive)
            {
                Unit* dead = bot->me->GetMap()->GetUnit(m.guid);
                Stop(commander, std::string(dead ? dead->GetName() : "a party member") + " is dead.");
                return;
            }
        }

        // A pull is being prepared or executed.
        if (plan.pull.phase != PullPhase::None)
            return;

        if (plan.lastPullOutcome == PullOutcome::Cancelled)
        {
            plan.lastPullOutcome = PullOutcome::None;
            Stop(commander, "the pull was cancelled.");
            return;
        }
        if (plan.lastPullOutcome == PullOutcome::Done)
        {
            plan.lastPullOutcome = PullOutcome::None;
            ++clear.cleared;
        }

        // Fight still going (also fights the player started), or just ended: wait.
        if (!world.enemies.empty())
        {
            clear.idleSinceMs = nowMs;
            return;
        }
        if (clear.idleSinceMs && WorldTimer::getMSTimeDiff(clear.idleSinceMs, nowMs) < PAUSE_AFTER_FIGHT_MS)
            return;

        // Next pack: packs are sorted nearest to the leader first.
        for (PackFacts const& pack : world.packs)
        {
            if (pack.distanceToLeader > clear.radius || IsSkipped(clear, pack))
                continue;

            Creature* first = nullptr;
            for (PackCreature const& c : pack.members)
            {
                if (!c.aggressive)
                    continue;
                if (Creature* creature = bot->me->GetMap()->GetCreature(c.guid))
                {
                    first = creature;
                    break;
                }
            }
            if (!first)
                continue;

            if (pack.maxLevel > leader->GetLevel() + MAX_LEVEL_ABOVE_LEADER)
            {
                Skip(clear, pack);
                Note(plan, std::string("Skipping ") + first->GetName() + "'s pack: level too high.");
                continue;
            }

            std::vector<std::string> report;
            if (!PullPlanner::Plan(leader, commander, first, false, report))
            {
                Skip(clear, pack);
                Note(plan, std::string("Skipping ") + first->GetName() + "'s pack: " + (report.empty() ? "cannot pull" : report.front()));
                continue;
            }

            if (plan.pull.expected.size() > clear.maxMobs)
            {
                char buf[200];
                snprintf(buf, sizeof(buf), "Skipping %s's pack: would bring %u (limit %u).",
                    first->GetName(), uint32(plan.pull.expected.size()), clear.maxMobs);
                plan.pull = PullPlan();
                Skip(clear, pack);
                Note(plan, buf);
                continue;
            }

            // Only the summary line; the full report is available through .partybot ai pull.
            if (!report.empty())
                Note(plan, report.front());
            return;
        }

        char buf[160];
        snprintf(buf, sizeof(buf), "nothing left to pull within %.0f yd (%u packs skipped in total).",
            clear.radius, clear.skippedPacks);
        Stop(commander, buf);
    }
}
}
