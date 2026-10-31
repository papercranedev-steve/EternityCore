#include "Commander/Coordination/PullPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace Commander
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

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        Player* FindTank(Player* leader)
        {
            Group* group = leader->GetGroup();
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* m = itr->getSource();
                if (!m || !m->IsInWorld() || m->GetMap() != leader->GetMap() || !m->IsAlive() || !m->AI())
                    continue;
                if (PartyBotAI const* ai = dynamic_cast<PartyBotAI const*>(m->AI()))
                    if (ai->m_role == ROLE_TANK)
                        return m;
            }
            return nullptr;
        }

        struct Option
        {
            Creature* pullTarget = nullptr;
            std::vector<ObjectGuid> expected;
            uint32 elites = 0;
            float distance = 0.0f;
        };
    }

    std::vector<PullPlanner::Issue> PullPlanner::CheckReady(WorldModel const& world)
    {
        std::vector<Issue> issues;
        PartyMemberFacts const* leader = world.FindMember(world.leader);
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            if (!m.alive)
            {
                issues.push_back({ m.guid, "dead", 0.0f });
                continue;
            }
            if (m.inCombat)
                issues.push_back({ m.guid, "combat", 0.0f });
            if (m.healthPct < READY_HEALTH_PCT)
                issues.push_back({ m.guid, "hp", m.healthPct });
            if (m.manaPct >= 0.0f && m.manaPct < READY_MANA_PCT)
                issues.push_back({ m.guid, "mana", m.manaPct });
            if (leader && m.guid != leader->guid)
            {
                float const d = PackResolver::Distance(m.pos, leader->pos);
                if (d > READY_MAX_DISTANCE)
                    issues.push_back({ m.guid, "far", d });
            }
        }
        return issues;
    }

    bool PullPlanner::Plan(Player* leader, PartyCommander& commander, Creature* selected, bool skipReady,
                           std::vector<std::string>& report)
    {
        WorldModel const& world = commander.GetWorld();
        Map* map = leader->GetMap();

        Player* tank = FindTank(leader);
        if (!tank)
        {
            report.push_back("No living tank bot in your party on this map (.partybot setrole <name> tank).");
            return false;
        }

        // Candidates: the selected creature's pack, or the nearest pack with a hostile member.
        std::vector<ObjectGuid> candidates;
        if (selected)
        {
            if (!selected->IsAlive() || selected->IsInCombat() || !tank->IsValidAttackTarget(selected))
            {
                report.push_back("Selected creature cannot be pulled (dead, in combat or not attackable).");
                return false;
            }
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == selected->GetObjectGuid())
                        for (PackCreature const& m : pack.members)
                            candidates.push_back(m.guid);
            if (candidates.empty())
                candidates.push_back(selected->GetObjectGuid());
        }
        else
        {
            for (PackFacts const& pack : world.packs)
            {
                bool const hostile = std::any_of(pack.members.begin(), pack.members.end(),
                    [](PackCreature const& c) { return c.aggressive; });
                if (!hostile)
                    continue;
                for (PackCreature const& m : pack.members)
                    candidates.push_back(m.guid);
                break;  // packs are sorted nearest first
            }
            if (candidates.empty())
            {
                report.push_back("No hostile pack within 50 yards.");
                return false;
            }
        }

        // Pick the member that brings the fewest creatures, then fewest elites, then nearest to the tank.
        Option best;
        for (ObjectGuid guid : candidates)
        {
            Creature* c = map->GetCreature(guid);
            if (!c || !c->IsAlive() || c->IsInCombat() || !tank->IsValidAttackTarget(c))
                continue;

            Option o;
            o.pullTarget = c;
            o.expected = AssistPredictor::Predict(c, tank);
            for (ObjectGuid g : o.expected)
                if (Creature* e = map->GetCreature(g))
                    if (e->IsElite())
                        ++o.elites;
            o.distance = tank->GetDistance(c);

            bool better = !best.pullTarget ||
                o.expected.size() < best.expected.size() ||
                (o.expected.size() == best.expected.size() && o.elites < best.elites) ||
                (o.expected.size() == best.expected.size() && o.elites == best.elites && o.distance < best.distance);
            if (better)
                best = std::move(o);
        }
        if (!best.pullTarget)
        {
            report.push_back("No pullable creature in that pack.");
            return false;
        }

        TeamPlan& plan = commander.GetPlanMutable();
        PullPlan& pull = plan.pull;
        pull = PullPlan();
        pull.target = best.pullTarget->GetObjectGuid();
        pull.tank = tank->GetObjectGuid();
        pull.expected = best.expected;
        pull.spot = PositionOf(leader);
        pull.returnToSpot = leader->GetDistance(best.pullTarget) > 15.0f;
        pull.phaseSinceMs = WorldTimer::getMSTime();
        pull.phase = skipReady ? PullPhase::Approach : PullPhase::WaitReady;

        std::string names;
        for (ObjectGuid g : pull.expected)
        {
            if (!names.empty())
                names += ", ";
            Creature* e = map->GetCreature(g);
            names += e ? e->GetName() : "?";
        }
        report.push_back(Format("%s pulls %s: expect %u (%u elite): %s",
            tank->GetName(), best.pullTarget->GetName(), uint32(pull.expected.size()), best.elites, names.c_str()));
        if (candidates.size() > 1)
            report.push_back(Format("Chosen from %u pack members (fewest joiners).", uint32(candidates.size())));
        report.push_back(pull.returnToSpot ? "Tank brings them back to where you stand." : "Fight where they stand.");

        // Fight spot too close to a creature that is not part of the pull.
        for (PackFacts const& pack : world.packs)
        {
            for (PackCreature const& c : pack.members)
            {
                if (!c.aggressive || std::find(pull.expected.begin(), pull.expected.end(), c.guid) != pull.expected.end())
                    continue;
                Creature* other = map->GetCreature(c.guid);
                if (!other)
                    continue;
                float const d = PackResolver::Distance(pull.spot, c.pos);
                if (d < other->GetAttackDistance(leader) + 3.0f)
                    report.push_back(Format("Warning: %s is %.0f yd from the fight spot (aggro range %.0f).",
                        other->GetName(), d, other->GetAttackDistance(leader)));
            }
        }

        if (!skipReady)
            report.push_back("Waiting until everyone is ready (90% hp, 80% mana, within 40 yd). '.partybot ai pull now' skips this.");
        return true;
    }

    void PullAssigner::Clear(TeamPlan& plan) const
    {
        plan.pull = PullPlan();
    }

    void PullAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        PullPlan& pull = plan.pull;
        if (pull.phase == PullPhase::None)
            return;

        uint32 const now = world.refreshedAtMs;
        uint32 const elapsed = WorldTimer::getMSTimeDiff(pull.phaseSinceMs, now);
        auto enter = [&](PullPhase phase)
        {
            pull.phase = phase;
            pull.phaseSinceMs = now;
        };
        auto cancel = [&](std::string const& why)
        {
            pull.notes.push_back("Pull cancelled: " + why);
            pull.phase = PullPhase::None;
        };

        PartyMemberFacts const* tank = world.FindMember(pull.tank);
        if (!tank || !tank->alive)
        {
            cancel("tank is gone or dead.");
            return;
        }

        bool const fightStarted = std::any_of(pull.expected.begin(), pull.expected.end(),
            [&world](ObjectGuid g) { return world.FindEnemy(g) != nullptr; });

        switch (pull.phase)
        {
            case PullPhase::WaitReady:
            {
                if (fightStarted || !world.enemies.empty())
                {
                    cancel("a fight started.");
                    return;
                }
                if (PullPlanner::CheckReady(world).empty())
                {
                    pull.notes.push_back("Everyone ready, pulling.");
                    enter(PullPhase::Approach);
                }
                else if (elapsed > WAIT_READY_TIMEOUT_MS)
                    cancel("not ready after 90 seconds (.partybot ai ready shows why).");
                return;
            }
            case PullPhase::Approach:
                if (fightStarted)
                    enter(pull.returnToSpot ? PullPhase::Return : PullPhase::Settle);
                else if (elapsed > APPROACH_TIMEOUT_MS)
                    cancel("tank did not reach the target in 20 seconds.");
                return;
            case PullPhase::Return:
                if (PackResolver::Distance(tank->pos, pull.spot) <= RETURN_ARRIVED || elapsed > RETURN_TIMEOUT_MS)
                    enter(PullPhase::Settle);
                return;
            case PullPhase::Settle:
                if (elapsed >= SETTLE_MS || world.enemies.empty())
                    pull.phase = PullPhase::None;
                return;
            case PullPhase::None:
                return;
        }
    }
}
