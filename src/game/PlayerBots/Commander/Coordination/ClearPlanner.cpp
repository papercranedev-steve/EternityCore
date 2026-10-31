#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <list>

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

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        template <class Pred>
        bool AnyMember(PackFacts const& pack, Pred pred)
        {
            return std::any_of(pack.members.begin(), pack.members.end(), pred);
        }

        bool IsSkipped(ClearPlan const& clear, PackFacts const& pack)
        {
            return AnyMember(pack, [&clear](PackCreature const& c) { return Contains(clear.skipped, c.guid); });
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

        std::string NamesOf(Map* map, std::vector<ObjectGuid> const& guids)
        {
            std::string names;
            for (ObjectGuid g : guids)
            {
                if (!names.empty())
                    names += ", ";
                Unit* u = map->GetUnit(g);
                names += u ? u->GetName() : "a party member";
            }
            return names;
        }

        bool IsStealthedPackCreature(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == guid)
                        return c.stealthed;
            return false;
        }

        // Corpse with loot left on it (the client shows it as lootable).
        class LootableCorpseCheck
        {
        public:
            LootableCorpseCheck(WorldObject const* center, float range) : m_center(center), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c->IsDead() && c->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) &&
                    m_center->IsWithinDist(c, m_range, false);
            }

        private:
            WorldObject const* m_center;
            float m_range;
        };

        // A player of the party (bots never loot) may still loot a corpse near the leader.
        // Player::IsAllowedToLoot is the server's own rule: loot left, present at the kill,
        // group has loot rights, loot method (round robin, ...).
        bool HasLootLeft(Player* leader, WorldModel const& world, Map* map)
        {
            std::list<Creature*> corpses;
            LootableCorpseCheck check(leader, LOOT_RADIUS);
            MaNGOS::CreatureListSearcher<LootableCorpseCheck> searcher(corpses, check);
            Cell::VisitGridObjects(leader, searcher, LOOT_RADIUS);
            if (corpses.empty())
                return false;

            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || m.isBot)
                    continue;
                Player* player = map->GetPlayer(m.guid);
                if (!player || !player->IsInWorld())
                    continue;
                for (Creature* corpse : corpses)
                    if (player->IsAllowedToLoot(corpse))
                        return true;
            }
            return false;
        }

        bool HasLivingTank(WorldModel const& world)
        {
            return std::any_of(world.party.begin(), world.party.end(), [](PartyMemberFacts const& m)
            {
                return m.isBot && !m.isPet && m.alive && m.role == ROLE_TANK;
            });
        }

        // Leader moved away from the anchor. Outside a pull this only pauses: the leader is
        // usually looting a corpse; once standing still the clear goes on from there.
        // Returns false while paused (or stopped).
        bool FollowLeader(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Player* leader, uint32 nowMs)
        {
            ClearPlan& clear = plan.clear;
            Position3 const here = PositionOf(leader);
            if (!clear.leaderStillSinceMs || PackResolver::Distance(here, clear.leaderPos) >= LEADER_STILL_RADIUS)
            {
                clear.leaderPos = here;
                clear.leaderStillSinceMs = nowMs;
            }

            if (PackResolver::Distance(here, clear.anchor) <= MAX_LEADER_DRIFT)
            {
                if (clear.paused)
                {
                    clear.paused = false;
                    Note(plan, "Clear resumed.");
                }
                return true;
            }

            PullPhase const phase = plan.pull.phase;
            if (phase != PullPhase::None && phase != PullPhase::WaitReady)
            {
                Stop(commander, "you moved away from the starting point during a pull.");
                return false;
            }
            if (!clear.paused)
            {
                clear.paused = true;
                // The waiting pull was planned for the old fight spot: plan again after resuming.
                if (phase == PullPhase::WaitReady)
                    plan.pull = PullPlan();
                Note(plan, Format("Clear paused: you are more than %.0f yd from the start. "
                    "It goes on from where you stand still for %u seconds.", MAX_LEADER_DRIFT, LEADER_STILL_MS / 1000));
            }
            if (world.enemies.empty() && WorldTimer::getMSTimeDiff(clear.leaderStillSinceMs, nowMs) >= LEADER_STILL_MS)
            {
                clear.anchor = here;
                clear.paused = false;
                Note(plan, "Clear resumed from here.");
                return true;
            }
            return false;
        }

        // After a fight: dead members get up to REVIVE_WAIT_MS to be resurrected (the original
        // PartyBot AI does that). The second fight with a death stops the clear.
        // Returns true when everyone is alive.
        bool WaitForResurrection(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map, uint32 nowMs)
        {
            ClearPlan& clear = plan.clear;
            std::vector<ObjectGuid> dead;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && !m.alive)
                    dead.push_back(m.guid);

            if (dead.empty())
            {
                if (clear.reviveWaitSinceMs)
                {
                    clear.reviveWaitSinceMs = 0;
                    Note(plan, "Everyone is alive again, clear goes on.");
                }
                return true;
            }

            std::string const names = NamesOf(map, dead);
            if (!clear.reviveWaitSinceMs)
            {
                ++clear.deathEvents;
                if (clear.deathEvents >= MAX_DEATH_EVENTS)
                {
                    Stop(commander, names + " died (second fight with a death).");
                    return false;
                }
                clear.reviveWaitSinceMs = nowMs;
                Note(plan, Format("%s died. Waiting up to %u seconds for a resurrection.", names.c_str(), REVIVE_WAIT_MS / 1000));
                return false;
            }
            if (WorldTimer::getMSTimeDiff(clear.reviveWaitSinceMs, nowMs) > REVIVE_WAIT_MS)
                Stop(commander, Format("%s not resurrected within %u seconds.", names.c_str(), REVIVE_WAIT_MS / 1000));
            return false;
        }

        // The tank is about to go (WaitReady -> Approach this refresh). Patrols may have walked
        // next to the target while the party got ready: predict again. Over the limit: back to
        // WaitReady; still over after REPREDICT_WAIT_MS: skip the pack.
        void RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
        {
            PullPlan& pull = plan.pull;
            ClearPlan& clear = plan.clear;
            if (pull.phase != PullPhase::Approach || !pull.recheckOnApproach || pull.rechecked)
                return;

            Creature* target = map->GetCreature(pull.target);
            Unit* tank = map->GetUnit(pull.tank);
            if (!target || !tank || !target->IsAlive() || target->IsInCombat())
            {
                pull.rechecked = true;      // the assigner deals with it (fight started / timeout)
                return;
            }

            std::vector<ObjectGuid> expected = AssistPredictor::Predict(target, tank);
            // Stealthed pack members were added by the planner, keep counting them.
            for (ObjectGuid g : pull.expected)
                if (!Contains(expected, g) && IsStealthedPackCreature(world, g))
                    expected.push_back(g);

            if (expected.size() > clear.maxMobs)
            {
                if (!pull.recheckWaitSinceMs)
                {
                    pull.recheckWaitSinceMs = nowMs;
                    Note(plan, Format("Holding the pull: %s would now bring %u (limit %u). Waiting for that to change.",
                        target->GetName(), uint32(expected.size()), clear.maxMobs));
                }
                else if (WorldTimer::getMSTimeDiff(pull.recheckWaitSinceMs, nowMs) > REPREDICT_WAIT_MS)
                {
                    for (ObjectGuid g : pull.expected)
                        clear.skipped.push_back(g);
                    for (ObjectGuid g : expected)
                        clear.skipped.push_back(g);
                    ++clear.skippedPacks;
                    Note(plan, Format("Skipping %s's pack: still %u after %u seconds.",
                        target->GetName(), uint32(expected.size()), REPREDICT_WAIT_MS / 1000));
                    plan.pull = PullPlan();
                    return;
                }
                pull.phase = PullPhase::WaitReady;
                pull.phaseSinceMs = nowMs;
                return;
            }

            if (pull.recheckWaitSinceMs)
                Note(plan, Format("%s would bring %u now, pulling.", target->GetName(), uint32(expected.size())));
            pull.expected = std::move(expected);
            pull.rechecked = true;
        }

        uint32 CountElites(Map* map, std::vector<ObjectGuid> const& guids)
        {
            uint32 elites = 0;
            for (ObjectGuid g : guids)
                if (Creature* c = map->GetCreature(g))
                    if (c->IsElite())
                        ++elites;
            return elites;
        }

        // First member to hand to the planner: hostile, starts fights, visible to the tank.
        Creature* PickFirst(PackFacts const& pack, Map* map)
        {
            Creature* fallback = nullptr;
            for (PackCreature const& c : pack.members)
            {
                if (!c.aggressive)
                    continue;
                Creature* creature = map->GetCreature(c.guid);
                if (!creature)
                    continue;
                if (!c.stealthed && !c.noAggro)
                    return creature;
                if (!fallback)
                    fallback = creature;
            }
            return fallback;
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
        Map* map = bot->me->GetMap();
        Player* leader = bot->GetPartyLeader();
        if (!leader || !leader->IsInWorld() || leader->GetMap() != map)
        {
            Stop(commander, "you left the map.");
            return;
        }

        // Any fight (also one the player started, also while paused) restarts the pause after it.
        if (!world.enemies.empty())
            clear.idleSinceMs = nowMs;

        if (!FollowLeader(plan, commander, world, leader, nowMs))
            return;

        // A pull is being prepared or executed.
        if (plan.pull.phase != PullPhase::None)
        {
            RecheckBeforeApproach(plan, world, map, nowMs);
            return;
        }

        switch (plan.lastPullOutcome)
        {
            case PullOutcome::Cancelled:
                // Real failure (tank gone, not ready in time, target unreachable): the reason was noted.
                plan.lastPullOutcome = PullOutcome::None;
                Stop(commander, "the pull failed.");
                return;
            case PullOutcome::Interrupted:
                // Another fight came first. Wait for it; it may even be the planned pack.
                plan.lastPullOutcome = PullOutcome::None;
                clear.interruptedPack = plan.pull.expected;
                break;
            case PullOutcome::Done:
                plan.lastPullOutcome = PullOutcome::None;
                ++clear.cleared;
                break;
            case PullOutcome::None:
                break;
        }

        // Fight still going (also fights the player started): wait.
        if (!world.enemies.empty())
            return;

        // The interrupting fight killed the planned pack: it counts as cleared.
        if (!clear.interruptedPack.empty())
        {
            bool const allDead = std::all_of(clear.interruptedPack.begin(), clear.interruptedPack.end(),
                [map](ObjectGuid g) { Creature* c = map->GetCreature(g); return !c || !c->IsAlive(); });
            if (allDead)
                ++clear.cleared;
            clear.interruptedPack.clear();
        }

        if (!WaitForResurrection(plan, commander, world, map, nowMs))
            return;

        // Short pause after a fight, longer while the player still has corpses to loot.
        if (clear.idleSinceMs)
        {
            uint32 const sinceFight = WorldTimer::getMSTimeDiff(clear.idleSinceMs, nowMs);
            if (sinceFight < PAUSE_AFTER_FIGHT_MS)
                return;
            if (sinceFight < LOOT_WAIT_MAX_MS && HasLootLeft(leader, world, map))
                return;
        }

        // Not a problem of any pack: skipping them one by one would end with "nothing left".
        if (!HasLivingTank(world))
        {
            Stop(commander, "no living tank bot in your party on this map (.partybot setrole <name> tank).");
            return;
        }

        // Next pack: packs are sorted nearest to the leader first. Packs with a patrol go last:
        // their membership changes as the patrol walks, they are looked at again every round.
        bool passedOver = false;    // a pack is left for later (someone else is fighting it)
        for (int pass = 0; pass < 2; ++pass)
        {
            bool const patrolPass = pass == 1;
            for (PackFacts const& pack : world.packs)
            {
                if (pack.distanceToLeader > clear.radius || IsSkipped(clear, pack))
                    continue;
                bool const patrol = AnyMember(pack, [](PackCreature const& c) { return c.patrol; });
                if (patrol != patrolPass)
                    continue;

                Creature* first = PickFirst(pack, map);
                if (!first)
                    continue;

                // Bosses and creatures that never start a fight are left to the player.
                bool const boss = AnyMember(pack, [](PackCreature const& c) { return c.boss; });
                bool const passive = !AnyMember(pack, [](PackCreature const& c) { return c.aggressive && !c.noAggro; });
                if (boss || passive)
                {
                    Skip(clear, pack);
                    Note(plan, std::string("Skipping ") + first->GetName() + "'s pack: " +
                        (boss ? "boss" : "never starts a fight") + ". Pull it yourself with .partybot ai pull.");
                    continue;
                }

                // Somebody else's fight: not a failure, the pack may be free later.
                if (AnyMember(pack, [](PackCreature const& c) { return c.tappedByOther; }))
                {
                    passedOver = true;
                    if (!Contains(clear.tappedNoted, first->GetObjectGuid()))
                    {
                        clear.tappedNoted.push_back(first->GetObjectGuid());
                        Note(plan, std::string("Passing over ") + first->GetName() + "'s pack for now: someone else is fighting it.");
                    }
                    continue;
                }

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

                // Hard pack (elite or several mobs): healers drink to full first. At low level this
                // takes seconds (combat scenario 13, 1.8). No cost model yet.
                bool const hard = CountElites(map, plan.pull.expected) > 0 || plan.pull.expected.size() >= HARD_PACK_MOBS;
                plan.pull.healerManaPct = hard ? PullPlanner::READY_FULL_MANA_PCT : PullPlanner::READY_MANA_PCT;
                plan.pull.recheckOnApproach = true;

                // Only the summary line, plus why the party fights at the pack if so;
                // the full report is available through .partybot ai pull.
                if (!report.empty())
                    Note(plan, report.front());
                for (std::string const& line : report)
                    if (line.find("casts from range") != std::string::npos)
                        Note(plan, line);
                if (hard)
                    Note(plan, "Healers drink to full mana first.");
                return;
            }
        }

        // Only packs other players are fighting are left: look again later.
        if (passedOver)
            return;

        Stop(commander, Format("nothing left to pull within %.0f yd (%u packs skipped in total).",
            clear.radius, clear.skippedPacks));
    }
}
}
