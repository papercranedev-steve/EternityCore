#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Shared/LootFacts.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Group.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <list>
#include <tuple>

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

        // A player of the party (bots never loot) may still loot a corpse near the leader and
        // would want to (Shared/LootFacts.h: the server's loot rules, gray-only corpses are
        // left behind as the player does).
        bool HasLootLeft(Player* leader, WorldModel const& world, Map* map)
        {
            std::vector<Creature*> const corpses = BotShared::LootFacts::LootableCorpsesNear(leader, LOOT_RADIUS);
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
                    if (player->IsAllowedToLoot(corpse) && BotShared::LootFacts::HasLootWorthTaking(player, corpse))
                        return true;
            }
            return false;
        }

        // ClearPlan::frontOnly: nothing in between on the walk, and in the leader's sight or a
        // short navmesh walk (FRONT_UNSEEN_WALK_YD: a prison cell next to him, stockade-20261125:
        // the server sees no line into the cells, the clear found nothing in front for 8 minutes).
        bool IsInFront(PackFacts const& pack)
        {
            if (!pack.route.blocker.IsEmpty())
                return false;
            return pack.route.leaderSees ||
                (pack.route.state == PackRouteState::Walk && pack.route.walkYd <= FRONT_UNSEEN_WALK_YD);
        }

        // A bot that can pull (Perception/Tanks.h).
        bool HasLivingTank(WorldModel const& world)
        {
            return Tanks::Main(world, true) != nullptr;
        }

        // The order packs are looked at (PackFacts::route, PackRoutes.h):
        //  1. packs without a patrol before packs with one: a patrol's pack changes as it walks,
        //     it is looked at again every round;
        //  2. packs whose way from the leader passes no other creature before those behind
        //     another pack: the one in front is pulled first, the way to the next is then open
        //     (in a corridor the corridor is cleared before the room beyond it);
        //  3. nearest walk first (straight distance where the walk is not known).
        std::vector<PackFacts const*> PullOrder(WorldModel const& world)
        {
            std::vector<PackFacts const*> order;
            order.reserve(world.packs.size());
            for (PackFacts const& pack : world.packs)
                order.push_back(&pack);
            auto const key = [](PackFacts const* p)
            {
                bool const patrol = AnyMember(*p, [](PackCreature const& c) { return c.patrol; });
                return std::make_tuple(patrol, !p->route.blocker.IsEmpty(), p->ApproachYd());
            };
            std::stable_sort(order.begin(), order.end(), [&key](PackFacts const* a, PackFacts const* b)
            {
                return key(a) < key(b);
            });
            return order;
        }

        // Leader moved away from the anchor. This only pauses: the leader is usually looting a
        // corpse or walking on; once he settles the clear goes on from there. A pull already
        // under way (the tank left) is finished first: the leader usually walks after the tank
        // to join the fight (recording strat-20261125-211343, stopping there ended the whole
        // clear). Returns false while paused.
        bool FollowLeader(TeamPlan& plan, WorldModel const& world, Player* leader, uint32 nowMs)
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
                return true;
            if (!clear.paused)
            {
                clear.paused = true;
                // The waiting pull was planned for the old fight spot: plan again after resuming.
                if (phase == PullPhase::WaitReady)
                    plan.pull = PullPlan();
                Note(plan, Format("Clear paused: you are more than %.0f yd from the start. "
                    "It goes on from where you stay (within %.0f yd) for %u seconds.", MAX_LEADER_DRIFT,
                    LEADER_STILL_RADIUS, LEADER_STILL_MS / 1000));
            }
            if (!world.HasPartyEnemy() && WorldTimer::getMSTimeDiff(clear.leaderStillSinceMs, nowMs) >= LEADER_STILL_MS)
            {
                clear.anchor = here;
                clear.paused = false;
                Note(plan, "Clear resumed from here.");
                return true;
            }
            return false;
        }

        // A standing clear (ClearPlan::standing): the anchor is wherever the leader is while
        // no pull is under way. He walks off before the tank reached the pack: the pull is
        // called off (he did not want that fight), the tank comes back. Returns false then.
        // Walking towards the pack (to join the fight) is fine: only walking away from it counts.
        bool TrackLeader(TeamPlan& plan, Player* leader, Map* map)
        {
            ClearPlan& clear = plan.clear;
            Position3 const here = PositionOf(leader);
            PullPhase const phase = plan.pull.phase;
            if (phase == PullPhase::None)
            {
                clear.anchor = here;
                clear.pullLeaderYd = -1.0f;
                return true;
            }
            if (phase != PullPhase::WaitReady && phase != PullPhase::Approach)
                return true;
            Creature const* target = map->GetCreature(plan.pull.target);
            if (!target)
                return true;
            float const yd = leader->GetDistance(target);
            if (clear.pullLeaderYd < 0.0f)
                clear.pullLeaderYd = yd;
            if (yd <= clear.pullLeaderYd + MAX_LEADER_DRIFT)
                return true;
            plan.pull = PullPlan();
            plan.lastPullOutcome = PullOutcome::None;
            clear.anchor = here;
            clear.pullLeaderYd = -1.0f;
            Note(plan, "Pull called off: you walked away from it.");
            return false;
        }

        // A standing clear runs for hours (ClearPlan::forgotAtMs, lastDeathMs).
        void ForgetOld(ClearPlan& clear, uint32 nowMs)
        {
            if (!clear.forgotAtMs)
                clear.forgotAtMs = nowMs;
            if (WorldTimer::getMSTimeDiff(clear.forgotAtMs, nowMs) >= FORGET_MS)
            {
                clear.forgotAtMs = nowMs;
                clear.skipped.clear();
                clear.skippedPacks = 0;
                clear.patrolFirstTried.clear();
                clear.tappedNoted.clear();
                clear.stealthNoted.clear();
                clear.noWayNoted.clear();
                clear.approved.clear();
            }
            if (clear.deathEvents && !clear.reviveWaitSinceMs &&
                WorldTimer::getMSTimeDiff(clear.lastDeathMs, nowMs) >= DEATH_MEMORY_MS)
                clear.deathEvents = 0;
        }

        bool AnyIn(PackFacts const& pack, std::vector<ObjectGuid> const& list)
        {
            return AnyMember(pack, [&list](PackCreature const& c) { return Contains(list, c.guid); });
        }

        // ClearPlan::asks: put the creatures up for a question instead of skipping them (one at
        // a time). False: another question is waiting.
        bool PutUp(ClearPlan& clear, std::vector<ObjectGuid> const& creatures, ObjectGuid first, PullAskKind kind,
                   uint32 count)
        {
            if (!clear.ask.pack.empty())
                return false;
            clear.ask.kind = kind;
            clear.ask.first = first;
            clear.ask.count = count;
            clear.ask.pack = creatures;
            return true;
        }

        bool PutUp(ClearPlan& clear, PackFacts const& pack, Creature* first, PullAskKind kind, uint32 count)
        {
            std::vector<ObjectGuid> members;
            for (PackCreature const& c : pack.members)
                members.push_back(c.guid);
            return PutUp(clear, members, first->GetObjectGuid(), kind, count);
        }

        // After a fight: dead members get up to REVIVE_WAIT_MS to be resurrected (the original
        // PartyBot AI does that). The second fight with a death stops the clear.
        // Returns true when everyone is alive.
        bool WaitForResurrection(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map, uint32 nowMs)
        {
            ClearPlan& clear = plan.clear;
            std::vector<ObjectGuid> dead;
            bool newDeath = false;      // someone who was alive when the clear started
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet)
                    continue;
                if (m.alive)
                {
                    // Up again: a later death of this member counts.
                    clear.deadAtStart.erase(std::remove(clear.deadAtStart.begin(), clear.deadAtStart.end(), m.guid),
                        clear.deadAtStart.end());
                    continue;
                }
                dead.push_back(m.guid);
                if (!Contains(clear.deadAtStart, m.guid))
                    newDeath = true;
            }

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
                // Members already dead at the start are waited for, but are not a death of this clear.
                if (newDeath)
                {
                    ++clear.deathEvents;
                    clear.lastDeathMs = nowMs;
                }
                if (clear.deathEvents >= MAX_DEATH_EVENTS)
                {
                    Stop(commander, names + " died (second fight with a death).");
                    return false;
                }
                clear.reviveWaitSinceMs = nowMs;
                Note(plan, Format("%s %s. Waiting up to %u seconds for a resurrection.", names.c_str(),
                    newDeath ? "died" : "is dead", REVIVE_WAIT_MS / 1000));
                return false;
            }
            if (WorldTimer::getMSTimeDiff(clear.reviveWaitSinceMs, nowMs) > REVIVE_WAIT_MS)
                Stop(commander, Format("%s not resurrected within %u seconds.", names.c_str(), REVIVE_WAIT_MS / 1000));
            return false;
        }

        // The tank is about to go (WaitReady -> Approach this refresh): predict again and look
        // at patrols (PullPlanner::RecheckBeforeApproach). Given up: skip the pack; too hard in
        // a clear that asks: put it to the player.
        // Patrol first (PullPlanner::FindPatrolFirst): the pack's pull makes way for the patrol's.
        // Not planned after all: not tried again, the pack's pull is planned again next round.
        void PullPatrolFirst(TeamPlan& plan, PartyCommander& commander, Map* map, Player* leader, ObjectGuid patrolGuid)
        {
            Creature* patrol = map->GetCreature(patrolGuid);
            Creature* pack = map->GetCreature(plan.pull.target);
            std::string const packName = pack ? pack->GetName() : "the";
            plan.pull = PullPlan();
            plan.lastPullOutcome = PullOutcome::None;
            std::vector<std::string> report;
            if (!patrol || !PullPlanner::Plan(leader, commander, patrol, false, report) || !plan.pull.ambush)
            {
                plan.pull = PullPlan();
                plan.clear.patrolFirstTried.push_back(patrolGuid);
                return;
            }
            plan.pull.patrolFirst = true;
            Note(plan, Format("Patrol first: %s comes by alone in about %.0f s, taking it before %s's pack.",
                patrol->GetName(), plan.pull.ambushWaitSec, packName.c_str()));
        }

        void RecheckBeforeApproach(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map,
                                   Player* leader, uint32 nowMs)
        {
            std::string why;
            ObjectGuid patrolFirst;
            PullPlanner::RecheckResult const result = PullPlanner::RecheckBeforeApproach(plan, world, map, nowMs, why, &patrolFirst);
            if (result == PullPlanner::RecheckResult::PatrolFirst)
            {
                PullPatrolFirst(plan, commander, map, leader, patrolFirst);
                return;
            }

            PullPlan& pull = plan.pull;
            if (pull.phase == PullPhase::WaitReady)
            {
                // A patrol-first ambush whose patrol walked on before the party was ready (no
                // spot on its way now): back to the pack (it may be taken first next time round).
                if (pull.patrolFirst && pull.ambushWaitSec < 0.0f)
                {
                    Creature* patrol = map->GetCreature(pull.target);
                    Note(plan, Format("Patrol first: %s walked on before everyone was ready; back to the pack.",
                        patrol ? patrol->GetName() : "the patrol"));
                    plan.pull = PullPlan();
                    return;
                }
                // While the party gets ready (drinking): a patrol that would join may come by
                // alone now. Its ambush may go early too (it counts from the patrol's arrival).
                if (!pull.ambush && !pull.patrolFirst &&
                    (!pull.patrolFirstCheckedMs ||
                     WorldTimer::getMSTimeDiff(pull.patrolFirstCheckedMs, nowMs) >= PullPlanner::PATROL_FIRST_CHECK_MS))
                {
                    pull.patrolFirstCheckedMs = nowMs;
                    Creature* target = map->GetCreature(pull.target);
                    Player* tank = map->GetPlayer(pull.tank);
                    ObjectGuid const patrol = target && tank && target->IsAlive() && !target->IsInCombat()
                        ? PullPlanner::FindPatrolFirst(plan, world, map, tank, target, pull.expected) : ObjectGuid();
                    if (!patrol.IsEmpty())
                        PullPatrolFirst(plan, commander, map, leader, patrol);
                }
                return;
            }
            if (result != PullPlanner::RecheckResult::GiveUp && result != PullPlanner::RecheckResult::TooHard)
                return;

            ClearPlan& clear = plan.clear;
            if (result == PullPlanner::RecheckResult::TooHard && clear.asks)
            {
                // (another question waiting: planned again after it)
                if (PutUp(clear, plan.pull.expected, plan.pull.target, PullAskKind::Hard, uint32(plan.pull.expected.size())))
                    Note(plan, "Asking you about it: " + why + ".");
                plan.pull = PullPlan();
                return;
            }
            for (ObjectGuid g : plan.pull.expected)
                clear.skipped.push_back(g);
            ++clear.skippedPacks;
            Creature* target = map->GetCreature(plan.pull.target);
            std::string const whose = target ? std::string(target->GetName()) + "'s" : "the";
            Note(plan, "Skipping " + whose + " pack: " + why + ".");
            plan.pull = PullPlan();
        }

        // First member to hand to the planner: hostile, starts fights, visible to the tank.
        // `neutralToo`: a chosen pack (ClearPlan::chosenOnly) may be neutral, a quest's.
        Creature* PickFirst(PackFacts const& pack, Map* map, bool neutralToo)
        {
            Creature* fallback = nullptr;
            for (PackCreature const& c : pack.members)
            {
                if (!c.aggressive && !neutralToo)
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

    void Start(Player* anchor, PartyCommander& commander, float radius, uint32 maxMobs, bool followsLeader)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        plan.clear = ClearPlan();
        plan.clear.active = true;
        plan.clear.followsLeader = followsLeader;
        plan.clear.anchor = PositionOf(anchor);
        plan.clear.radius = radius;
        plan.clear.maxMobs = maxMobs;
        plan.clear.idleSinceMs = 0;     // first pull right away
        plan.lastPullOutcome = PullOutcome::None;
        // Already dead now: waited for like any death, but not counted as a death of this clear.
        for (PartyMemberFacts const& m : commander.GetWorld().party)
            if (!m.isPet && !m.alive)
                plan.clear.deadAtStart.push_back(m.guid);
    }

    void Stop(PartyCommander& commander, std::string const& why, ClearEnd end)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        if (!plan.clear.active)
            return;
        char buf[200];
        snprintf(buf, sizeof(buf), "Clear stopped after %u packs: %s", plan.clear.cleared, why.c_str());
        plan.clear.active = false;
        plan.clear.lastEnd = end;
        Note(plan, buf);
    }

    void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        ClearPlan& clear = plan.clear;
        WorldModel const& world = commander.GetWorld();
        Map* map = bot->me->GetMap();

        // A single `.partybot ai pull` gets the same second prediction and patrol wait as the
        // clear; it has no mob limit and never gives up (a patrol that stays near is reported,
        // then the pull goes on).
        if (!clear.active)
        {
            if (plan.pull.phase != PullPhase::None)
            {
                std::string why;
                PullPlanner::RecheckBeforeApproach(plan, world, map, nowMs, why);
            }
            return;
        }

        Player* leader = bot->GetPartyLeader();
        if (!leader || !leader->IsInWorld() || leader->GetMap() != map)
        {
            Stop(commander, "you left the map.");
            return;
        }

        // Any fight (also one the player started, also while paused) restarts the pause after it.
        if (world.HasPartyEnemy())
            clear.idleSinceMs = nowMs;

        if (clear.standing)
            ForgetOld(clear, nowMs);
        if (clear.standing ? !TrackLeader(plan, leader, map)
                           : clear.followsLeader && !FollowLeader(plan, world, leader, nowMs))
            return;

        // A pull is being prepared or executed.
        if (plan.pull.phase != PullPhase::None)
        {
            RecheckBeforeApproach(plan, commander, world, map, leader, nowMs);
            return;
        }

        // A patrol-first pull that did not happen (the patrol did not come by, the plan no
        // longer fitted): the patrol is not tried again, the pack is planned again.
        if (plan.pull.patrolFirst &&
            (plan.lastPullOutcome == PullOutcome::Cancelled || plan.lastPullOutcome == PullOutcome::CalledOff))
        {
            plan.lastPullOutcome = PullOutcome::None;
            clear.patrolFirstTried.push_back(plan.pull.target);
            plan.pull = PullPlan();
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
            case PullOutcome::CalledOff:
                // The plan no longer fitted before the opener (the reason was noted). The same
                // pack would be planned the same way again: skip it, go on with the next.
                plan.lastPullOutcome = PullOutcome::None;
                for (ObjectGuid g : plan.pull.expected)
                    clear.skipped.push_back(g);
                ++clear.skippedPacks;
                break;
            case PullOutcome::None:
                break;
        }

        // Fight still going (also fights the player started): wait.
        if (world.HasPartyEnemy())
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

        // Next pack, in PullOrder: open way first, nearest walk first, patrols last.
        bool passedOver = false;    // a pack is left for later (someone else is fighting it)
        uint32 stealthOnly = 0;     // packs whose hostile members are all stealthed now
        uint32 noWay = 0;           // packs in range by straight line, no walkable way to them
        {
            for (PackFacts const* packPtr : PullOrder(world))
            {
                PackFacts const& pack = *packPtr;
                if (pack.distanceToLeader > clear.radius || IsSkipped(clear, pack))
                    continue;
                // (not chosen now: maybe later, nothing to note)
                if (clear.chosenOnly && !AnyIn(pack, clear.chosen))
                    continue;
                bool const approved = AnyIn(pack, clear.approved);
                // (waiting for the player's answer about it)
                if (!clear.ask.pack.empty() && AnyIn(pack, clear.ask.pack))
                    continue;

                Creature* first = PickFirst(pack, map, clear.chosenOnly);
                if (!first)
                    continue;

                // Behind a wall or on another floor: near by straight line, not reachable from
                // here. Not skipped: the leader may stand elsewhere later.
                if (pack.route.state == PackRouteState::NoWay)
                {
                    ++noWay;
                    if (!Contains(clear.noWayNoted, first->GetObjectGuid()))
                    {
                        clear.noWayNoted.push_back(first->GetObjectGuid());
                        Note(plan, std::string("Passing over ") + first->GetName() + "'s pack: no walkable way from you.");
                    }
                    continue;
                }
                // A long way round (the other side of a wall): out of range.
                if (pack.ApproachYd() > clear.radius)
                    continue;
                // Not in front of him: behind another pack (pulled once that one is dead) or
                // out of his sight and not a short walk. Looked at again every round.
                if (clear.frontOnly && !IsInFront(pack))
                    continue;

                // Bosses and creatures that never start a fight are left to the player.
                // (chosen packs: AutoClear decided, e.g. a neutral creature a quest needs)
                bool const boss = !approved && !clear.chosenOnly && AnyMember(pack, [](PackCreature const& c) { return c.boss; });
                bool const passive = !clear.chosenOnly && !AnyMember(pack, [](PackCreature const& c) { return c.aggressive && !c.noAggro; });
                if (boss && !passive && clear.asks)
                {
                    PutUp(clear, pack, first, PullAskKind::Boss, 0);
                    continue;
                }
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

                // Only stealthed creatures could start the fight: the tank cannot target them now.
                // Not skipped for good (they may unstealth or walk into a visible pack): looked at
                // again every round. They do not keep the clear running on their own.
                bool const visibleHostile = AnyMember(pack, [&clear](PackCreature const& c)
                {
                    return (clear.chosenOnly || (c.aggressive && !c.noAggro)) && !c.stealthed;
                });
                if (!visibleHostile)
                {
                    ++stealthOnly;
                    if (!Contains(clear.stealthNoted, first->GetObjectGuid()))
                    {
                        clear.stealthNoted.push_back(first->GetObjectGuid());
                        Note(plan, std::string("Passing over ") + first->GetName() +
                            "'s pack for now: only stealthed creatures, the tank cannot target them.");
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

                // Too hard even rested (the fight estimate), or over a limit the player gave:
                // asked about when the clear asks, else skipped. A yes lets it through.
                uint32 const count = uint32(plan.pull.expected.size());
                bool const overLimit = clear.maxMobs && count > clear.maxMobs;
                if (!approved && (plan.pull.hard || overLimit))
                {
                    if (!overLimit && clear.asks)
                    {
                        // (another question waiting: this one after it)
                        plan.pull = PullPlan();
                        if (PutUp(clear, pack, first, PullAskKind::Hard, count))
                            Note(plan, Format("Asking you about %s's pack: %u, too hard by the fight estimate.",
                                first->GetName(), count));
                        continue;
                    }
                    std::string const why = overLimit ? Format("would bring %u (limit %u)", count, clear.maxMobs)
                                                      : Format("%u, too hard by the fight estimate", count);
                    plan.pull = PullPlan();
                    Skip(clear, pack);
                    Note(plan, "Skipping " + std::string(first->GetName()) + "'s pack: " + why + ".");
                    continue;
                }

                // Healer mana before the pull: PullPlanner::Plan sets it from the fight estimate
                // (was: full before an elite or 3 mobs, else 80%).
                // PullPlanner::Plan sets recheckOnApproach (not a "pull now"): the second
                // prediction, the limit and the patrol wait run in RecheckBeforeApproach.

                // Only the summary line, plus why the party fights at the pack, a ranged pull and
                // a patrol if so; the full report is available through .partybot ai pull.
                if (!report.empty())
                    Note(plan, report.front());
                for (std::string const& line : report)
                {
                    if (line.find("casts from range") != std::string::npos ||
                        line.rfind("Ranged pull:", 0) == 0 || line.rfind("Patrol ", 0) == 0)
                        Note(plan, line);
                    else if (line.rfind("No ranged pull:", 0) == 0 && !clear.missingAmmoNoted)
                    {
                        clear.missingAmmoNoted = true;
                        Note(plan, line);
                    }
                }
                if (plan.pull.healerManaPct > PullPlanner::READY_MANA_PCT)
                    Note(plan, Format("Healers drink to %.0f%% mana first (fight estimate).", plan.pull.healerManaPct));
                return;
            }
        }

        // Only packs other players are fighting are left: look again later. A standing clear
        // waits for the next pack; a pack put up for a question waits for the answer.
        if (passedOver || clear.standing || !clear.ask.pack.empty())
            return;

        std::string const stealthText = stealthOnly
            ? Format(" %u pack(s) with only stealthed creatures left; pull them yourself.", stealthOnly) : "";
        std::string const noWayText = noWay
            ? Format(" %u pack(s) nearby with no walkable way from here.", noWay) : "";
        std::string const where = clear.frontOnly ? Format("in front of you (in your sight or within %.0f yd walk, nothing in between)", FRONT_UNSEEN_WALK_YD)
                                                  : Format("within %.0f yd walk", clear.radius);
        Stop(commander, Format("nothing left to pull %s (%u packs skipped in total).",
            where.c_str(), clear.skippedPacks) + stealthText + noWayText, ClearEnd::NothingLeft);
    }
}
}
