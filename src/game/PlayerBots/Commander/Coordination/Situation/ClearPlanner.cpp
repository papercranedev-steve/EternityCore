#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "PartyBotAI.h"
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

        // Gray (poor quality) items are vendor trash: the player often leaves them on the corpse.
        bool IsWorthLooting(LootItem const& item)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item.itemid);
            return proto && proto->Quality > ITEM_QUALITY_POOR;
        }

        // The player's own copy of item `index` (FFA, quest or conditional list) is still on the corpse.
        bool HasUntakenEntry(QuestItemMap const& lists, uint32 playerLow, size_t index)
        {
            QuestItemMap::const_iterator const itr = lists.find(playerLow);
            if (itr == lists.end())
                return false;
            for (QuestItem const& qi : *itr->second)
                if (qi.index == index && !qi.is_looted)
                    return true;
            return false;
        }

        // A shared item (not FFA, not conditional) the player may take from the window.
        // Same rule as Player::IsAllowedToLoot: when another member (often a bot) is the round
        // robin looter, the player only gets items over the group loot threshold (they are rolled).
        bool MayTakeSharedItem(Player const* player, Loot const& loot, LootItem const& item)
        {
            if (!loot.roundRobinPlayer || loot.roundRobinPlayer == player->GetGUID())
                return true;
            Group const* group = player->GetGroup();
            if (!group)
                return true;
            switch (group->GetLootMethod())
            {
                case ROUND_ROBIN:
                    return false;
                case GROUP_LOOT:
                case NEED_BEFORE_GREED:
                    return !item.is_underthreshold;
                default:
                    return true;
            }
        }

        // Something on the corpse the player would take: money, one of their quest items, or an
        // item above gray quality they may take. Loot is rolled at the kill (Unit.cpp,
        // Creature::GenerateLootForBody), so the contents are known before anyone opens the corpse.
        // Items being rolled for (is_blocked) do not need the corpse any more.
        bool HasLootWorthTaking(Player const* player, Creature const* corpse)
        {
            Loot const& loot = corpse->loot;
            if (loot.gold)
                return true;

            uint32 const playerLow = player->GetGUIDLow();
            QuestItemMap::const_iterator const quest = loot.GetPlayerQuestItems().find(playerLow);
            if (quest != loot.GetPlayerQuestItems().end())
                for (QuestItem const& qi : *quest->second)
                    if (!qi.is_looted && qi.index < loot.m_questItems.size() && !loot.m_questItems[qi.index].is_looted)
                        return true;

            for (size_t i = 0; i < loot.items.size(); ++i)
            {
                LootItem const& item = loot.items[i];
                if (item.is_looted || item.is_blocked || !IsWorthLooting(item))
                    continue;
                if (item.freeforall)
                {
                    if (HasUntakenEntry(loot.GetPlayerFFAItems(), playerLow, i))
                        return true;
                }
                else if (item.conditionId)
                {
                    if (HasUntakenEntry(loot.GetPlayerNonQuestNonFFAConditionalItems(), playerLow, i))
                        return true;
                }
                else if (item.AllowedForPlayer(player, corpse) && MayTakeSharedItem(player, loot, item))
                    return true;
            }
            return false;
        }

        // A player of the party (bots never loot) may still loot a corpse near the leader and
        // would want to. Player::IsAllowedToLoot is the server's own rule: loot left, present at
        // the kill, group has loot rights, loot method (round robin, ...). HasLootWorthTaking
        // drops corpses with only gray items left, which the player leaves behind.
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
                    if (player->IsAllowedToLoot(corpse) && HasLootWorthTaking(player, corpse))
                        return true;
            }
            return false;
        }

        // A bot that can pull (Perception/Tanks.h).
        bool HasLivingTank(WorldModel const& world)
        {
            return Tanks::Main(world, true) != nullptr;
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
            if (!world.HasPartyEnemy() && WorldTimer::getMSTimeDiff(clear.leaderStillSinceMs, nowMs) >= LEADER_STILL_MS)
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
                    ++clear.deathEvents;
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
        // at patrols (PullPlanner::RecheckBeforeApproach). Given up (still over the limit, or a
        // patrol still near after waiting): skip the pack.
        void RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
        {
            std::string why;
            if (PullPlanner::RecheckBeforeApproach(plan, world, map, nowMs, why) != PullPlanner::RecheckResult::GiveUp)
                return;

            ClearPlan& clear = plan.clear;
            for (ObjectGuid g : plan.pull.expected)
                clear.skipped.push_back(g);
            ++clear.skippedPacks;
            Creature* target = map->GetCreature(plan.pull.target);
            std::string const whose = target ? std::string(target->GetName()) + "'s" : "the";
            Note(plan, "Skipping " + whose + " pack: " + why + ".");
            plan.pull = PullPlan();
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
        // Already dead now: waited for like any death, but not counted as a death of this clear.
        for (PartyMemberFacts const& m : commander.GetWorld().party)
            if (!m.isPet && !m.alive)
                plan.clear.deadAtStart.push_back(m.guid);
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

        // Next pack: packs are sorted nearest to the leader first. Packs with a patrol go last:
        // their membership changes as the patrol walks, they are looked at again every round.
        bool passedOver = false;    // a pack is left for later (someone else is fighting it)
        uint32 stealthOnly = 0;     // packs whose hostile members are all stealthed now
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

                // Only stealthed creatures could start the fight: the tank cannot target them now.
                // Not skipped for good (they may unstealth or walk into a visible pack): looked at
                // again every round. They do not keep the clear running on their own.
                bool const visibleHostile = AnyMember(pack, [](PackCreature const& c)
                {
                    return c.aggressive && !c.noAggro && !c.stealthed;
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

        // Only packs other players are fighting are left: look again later.
        if (passedOver)
            return;

        std::string const stealthText = stealthOnly
            ? Format(" %u pack(s) with only stealthed creatures left; pull them yourself.", stealthOnly) : "";
        Stop(commander, Format("nothing left to pull within %.0f yd (%u packs skipped in total).",
            clear.radius, clear.skippedPacks) + stealthText);
    }
}
}
