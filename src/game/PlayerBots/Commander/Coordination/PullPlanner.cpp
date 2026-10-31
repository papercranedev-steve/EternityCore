#include "Commander/Coordination/PullPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ProfileStore.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        PackFacts const* FindPackOf(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == guid)
                        return &pack;
            return nullptr;
        }

        PackCreature const* FindMemberFacts(PackFacts const* pack, ObjectGuid guid)
        {
            if (!pack)
                return nullptr;
            for (PackCreature const& c : pack->members)
                if (c.guid == guid)
                    return &c;
            return nullptr;
        }

        template <class Pred>
        bool AnyMember(PackFacts const& pack, Pred pred)
        {
            return std::any_of(pack.members.begin(), pack.members.end(), pred);
        }

        // Name of a party member for notes written by the pure assigner.
        std::string NameIn(PullPlan const& pull, WorldModel const& world, ObjectGuid guid)
        {
            if (guid == world.leader)
                return "you";
            for (PullMemberName const& n : pull.names)
                if (n.guid == guid)
                    return n.name;
            return "a party member";
        }

        std::string DescribeIssues(std::vector<PullPlanner::Issue> const& issues, WorldModel const& world, PullPlan const& pull)
        {
            std::string text;
            for (PullPlanner::Issue const& i : issues)
            {
                if (!text.empty())
                    text += ", ";
                text += NameIn(pull, world, i.who);
                if (!strcmp(i.what, "hp") || !strcmp(i.what, "mana"))
                    text += Format(" %s %.0f%%", i.what, i.value);
                else if (!strcmp(i.what, "far"))
                    text += Format(" %.0f yd away", i.value);
                else
                    text += std::string(" ") + i.what;
            }
            return text;
        }

        // Enemy in the fight that the pull did not predict. Adds summoned by a predicted
        // creature belong to the pull.
        bool IsUnexpected(EnemyFacts const& e, PullPlan const& pull)
        {
            if (Contains(pull.expected, e.guid))
                return false;
            return !(e.summoned && Contains(pull.expected, e.summoner));
        }
    }

    std::vector<PullPlanner::Issue> PullPlanner::CheckReady(WorldModel const& world, float healerManaPct)
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
            // Healers may need more than the others before a hard pack (ClearPlanner raises it).
            float const manaNeeded = (m.isBot && m.role == ROLE_HEALER) ? healerManaPct : READY_MANA_PCT;
            if (m.manaPct >= 0.0f && m.manaPct < manaNeeded)
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
        PackFacts const* pack = nullptr;
        std::vector<ObjectGuid> candidates;
        std::vector<std::string> notes;     // reported after the summary line
        if (selected)
        {
            if (!selected->IsAlive() || selected->IsInCombat() || !tank->IsValidAttackTarget(selected))
            {
                report.push_back("Selected creature cannot be pulled (dead, in combat or not attackable).");
                return false;
            }
            pack = FindPackOf(world, selected->GetObjectGuid());
            if (pack)
            {
                // Somebody else has loot rights: their fight, not ours.
                if (AnyMember(*pack, [](PackCreature const& c) { return c.tappedByOther; }))
                {
                    report.push_back(Format("Someone else is fighting %s's pack (they have loot rights).", selected->GetName()));
                    return false;
                }
                // Bosses are left to the player, but selecting one is an explicit choice: allowed.
                if (AnyMember(*pack, [](PackCreature const& c) { return c.boss; }))
                    notes.push_back("Note: this pack has a boss. Pulling it because you selected it.");
                for (PackCreature const& m : pack->members)
                    candidates.push_back(m.guid);
            }
            else
            {
                std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(selected->GetEntry());
                if (profile && profile->boss)
                    notes.push_back("Note: this is a boss. Pulling it because you selected it.");
                candidates.push_back(selected->GetObjectGuid());
            }
        }
        else
        {
            uint32 tapped = 0;
            uint32 bosses = 0;
            for (PackFacts const& p : world.packs)
            {
                // No-aggro creatures never start a fight, so they do not make a pack worth pulling.
                bool const hostile = AnyMember(p, [](PackCreature const& c) { return c.aggressive && !c.noAggro; });
                if (!hostile)
                    continue;
                if (AnyMember(p, [](PackCreature const& c) { return c.boss; }))
                {
                    ++bosses;       // only when the player selects it
                    continue;
                }
                if (AnyMember(p, [](PackCreature const& c) { return c.tappedByOther; }))
                {
                    ++tapped;
                    continue;
                }
                pack = &p;
                break;  // packs are sorted nearest first
            }
            if (bosses)
                notes.push_back(Format("Passed over %u pack(s) with a boss; select the boss to pull it.", bosses));
            if (tapped)
                notes.push_back(Format("Passed over %u pack(s) someone else is fighting.", tapped));
            if (!pack)
            {
                report.push_back("No hostile pack within 50 yards.");
                report.insert(report.end(), notes.begin(), notes.end());
                return false;
            }
            for (PackCreature const& m : pack->members)
                candidates.push_back(m.guid);
        }

        // Pick the member that brings the fewest creatures, then fewest elites, then nearest to the tank.
        Option best;
        uint32 stealthed = 0;
        for (ObjectGuid guid : candidates)
        {
            Creature* c = map->GetCreature(guid);
            if (!c || !c->IsAlive() || c->IsInCombat() || !tank->IsValidAttackTarget(c))
                continue;
            // The tank cannot target a stealthed creature and would walk for 20 seconds.
            PackCreature const* facts = FindMemberFacts(pack, guid);
            if (facts && facts->stealthed)
            {
                ++stealthed;
                continue;
            }

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
            report.push_back(stealthed ? "Only stealthed creatures left in that pack; the tank cannot target them."
                                       : "No pullable creature in that pack.");
            return false;
        }

        // Stealthed pack members still join the fight: count them.
        uint32 stealthedAdded = 0;
        if (pack)
        {
            for (PackCreature const& c : pack->members)
            {
                if (c.stealthed && !Contains(best.expected, c.guid))
                {
                    best.expected.push_back(c.guid);
                    if (c.elite)
                        ++best.elites;
                    ++stealthedAdded;
                }
            }
        }

        // Casters that stand back and cast do not follow the tank: bringing them back is useless.
        Creature const* rangedCaster = nullptr;
        for (ObjectGuid g : best.expected)
        {
            Creature* e = map->GetCreature(g);
            if (!e)
                continue;
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e->GetEntry());
            if (profile && profile->mainRangedSpellId)
            {
                rangedCaster = e;
                break;
            }
        }

        TeamPlan& plan = commander.GetPlanMutable();
        PullPlan& pull = plan.pull;
        pull = PullPlan();
        plan.lastPullOutcome = PullOutcome::None;
        pull.target = best.pullTarget->GetObjectGuid();
        pull.tank = tank->GetObjectGuid();
        pull.expected = best.expected;
        pull.spot = PositionOf(leader);
        bool const distant = leader->GetDistance(best.pullTarget) > RETURN_MIN_DISTANCE;   // `far` is a Windows macro
        pull.returnToSpot = distant && !rangedCaster;
        pull.phaseSinceMs = WorldTimer::getMSTime();
        pull.phase = skipReady ? PullPhase::Approach : PullPhase::WaitReady;
        pull.healerManaPct = READY_MANA_PCT;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            if (Unit* u = map->GetUnit(m.guid))
                pull.names.push_back({ m.guid, u->GetName() });
        }

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
        if (stealthedAdded)
            report.push_back(Format("Counted %u stealthed creature(s) of the pack.", stealthedAdded));
        if (pull.returnToSpot)
            report.push_back("Tank brings them back to where you stand.");
        else if (distant && rangedCaster)
            report.push_back(Format("Fight where they stand: %s casts from range and would not follow the tank.",
                rangedCaster->GetName()));
        else
            report.push_back("Fight where they stand.");
        report.insert(report.end(), notes.begin(), notes.end());

        // Fight spot too close to a creature that is not part of the pull.
        for (PackFacts const& p : world.packs)
        {
            for (PackCreature const& c : p.members)
            {
                if (!c.aggressive || Contains(pull.expected, c.guid))
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
        plan.clear = ClearPlan();
        plan.lastPullOutcome = PullOutcome::None;
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
        // `pull.expected` is kept: ClearPlanner reads it after an interrupted pull.
        auto finish = [&](PullOutcome outcome, std::string const& text)
        {
            plan.notes.push_back(text);
            pull.phase = PullPhase::None;
            plan.lastPullOutcome = outcome;
        };
        auto cancel = [&](std::string const& why) { finish(PullOutcome::Cancelled, "Pull cancelled: " + why); };
        auto interrupt = [&](std::string const& why)
        {
            finish(PullOutcome::Interrupted, "Pull interrupted: " + why +
                (plan.clear.active ? " Clear goes on after this fight." : ""));
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
                // The player or a patrol started a fight: not a failure of the plan.
                if (fightStarted || !world.enemies.empty())
                {
                    interrupt("a fight started.");
                    return;
                }
                std::vector<PullPlanner::Issue> const issues = PullPlanner::CheckReady(world, pull.healerManaPct);
                if (issues.empty())
                {
                    // Announce once: ClearPlanner may send the pull back here after a new prediction.
                    if (!pull.readyAnnounced)
                        plan.notes.push_back("Everyone ready, pulling.");
                    pull.readyAnnounced = true;
                    enter(PullPhase::Approach);
                    return;
                }

                // Only bot problems count toward the timeout; players do not eat or drink on
                // their own command, so their state is reported instead.
                bool const botIssue = std::any_of(issues.begin(), issues.end(), [&world](PullPlanner::Issue const& i)
                {
                    PartyMemberFacts const* m = world.FindMember(i.who);
                    return !m || m->isBot;
                });
                if (botIssue && pull.lastReadyCheckMs)
                    pull.botWaitMs += WorldTimer::getMSTimeDiff(pull.lastReadyCheckMs, now);
                pull.lastReadyCheckMs = now;

                if (!pull.readyNoteSent && elapsed > WAIT_READY_NOTE_MS)
                {
                    pull.readyNoteSent = true;
                    plan.notes.push_back("Still waiting for: " + DescribeIssues(issues, world, pull) +
                        (botIssue ? "." : ". (Your own state does not time out.)"));
                }
                if (pull.botWaitMs > WAIT_READY_TIMEOUT_MS)
                    cancel("bots not ready after 90 seconds (.partybot ai ready shows why).");
                return;
            }
            case PullPhase::Approach:
                if (fightStarted)
                    enter(pull.returnToSpot ? PullPhase::Return : PullPhase::Settle);
                // Someone else started another fight: the tank must not go on to pull a second pack.
                else if (std::any_of(world.enemies.begin(), world.enemies.end(),
                         [&pull](EnemyFacts const& e) { return IsUnexpected(e, pull); }))
                    interrupt("a fight with other enemies started.");
                else if (elapsed > APPROACH_TIMEOUT_MS)
                    cancel("tank did not reach the target in 20 seconds.");
                return;
            case PullPhase::Return:
                if (PackResolver::Distance(tank->pos, pull.spot) <= RETURN_ARRIVED || elapsed > RETURN_TIMEOUT_MS)
                    enter(PullPhase::Settle);
                return;
            case PullPhase::Settle:
                if (elapsed >= SETTLE_MS || world.enemies.empty())
                {
                    pull.phase = PullPhase::None;
                    plan.lastPullOutcome = PullOutcome::Done;
                }
                return;
            case PullPhase::None:
                return;
        }
    }
}
