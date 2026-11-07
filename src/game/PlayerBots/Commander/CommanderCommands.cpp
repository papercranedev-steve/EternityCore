/*
 * .partybot ai [subcommand]   GM debug commands for the Commander.
 *
 *   .partybot ai                     status
 *   .partybot ai on | off            enable / disable the whole Commander
 *   .partybot ai interval <ms>       bot AI update interval (100-5000), runtime only
 *   .partybot ai stats [reset]       cost of PartyBotAI::UpdateAI ticks
 *   .partybot ai modules             list behavior modules
 *   .partybot ai module <id> on|off  toggle one module
 *   .partybot ai reload              re-read PartyBot.Commander.* (run .reload config first
 *                                    if mangosd.conf was edited)
 *   .partybot ai reload overrides    re-read custom_partybot_*_override, clear profile cache
 *   .partybot ai bot [n]             state of the selected bot and its last n arbiter decisions
 *   .partybot ai profile             selected creature's profile (melee, spells, on-hit debuffs)
 *   .partybot ai spell <id> [level]  one spell's profile and amounts at a caster level
 *   .partybot ai rates               fight-wide rates and value units of your party
 *   .partybot ai control             bots' crowd control spells and what each is worth now (ControlValue.h)
 *   .partybot ai threat              enemies of your party: victim, threat, headroom per member
 *   .partybot ai threat watch [off|target|all]   addon threat meter feed (ThreatFeed.h)
 *   .partybot ai packs               out-of-combat packs within 50 yards
 *   .partybot ai plan                team plan: pull state, focus and intercepts, interrupt abilities and duties
 *   .partybot ai ready               who is not ready to pull (hp, mana, distance, dead)
 *   .partybot ai pull [now|cancel]   bots plan and pull your selection's pack (or the nearest
 *                                    hostile pack) once everyone is ready; "now" skips the wait
 *   .partybot ai pullmethod [auto|ranged|melee]   how the tank opens your pulls and clears
 *                                    (PullMethod.h); no argument shows it
 *   .partybot ai clear [r] [n]       pull every pack within r yards one after another (at most
 *                                    n mobs per pull), waiting until ready in between; "clear stop"
 *   .partybot ai retreat [stop]      whole party stops attacking and runs to a point out of every
 *                                    enemy's threat area until they give up (outdoors only)
 *   .partybot ai healsim <n> [name]  predicted threat if the healer heals your selection for n
 *
 * GM commands run on the world thread while map threads are idle (MapManager::Update
 * waits for all maps), so reading world models and units here is safe.
 */

#include "Chat.h"
#include "Player.h"
#include "PartyBotAI.h"
#include "Commander/CommanderMgr.h"
#include "Commander/BotDirectives.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Coordination/RetreatPlanner.h"
#include "Commander/ProfileCommands.h"
#include "Creature.h"
#include <algorithm>
#include <cstdlib>
#include "SpellMgr.h"
#include "Group.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Timer.h"
#include <cmath>
#include <cstring>

namespace
{
    std::string NameOf(Map* map, ObjectGuid guid)
    {
        if (guid.IsEmpty())
            return "-";
        if (Unit* unit = map->GetUnit(guid))
            return unit->GetName();
        return guid.GetString();
    }

    std::string Num(float value)
    {
        if (std::isinf(value))
            return value > 0 ? "inf" : "-inf";
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", value);
        return buf;
    }

    char const* PackSourceName(Commander::PackSource source)
    {
        switch (source)
        {
            case Commander::PackSource::Database: return "db";
            case Commander::PackSource::Distance: return "dist";
            case Commander::PackSource::Mixed:    return "db+dist";
        }
        return "?";
    }

    std::shared_ptr<Commander::PartyCommander> CommanderFor(ChatHandler* handler, Player* player)
    {
        std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(player);
        if (!commander)
            handler->SendSysMessage("No commander for your party on this map (no party bot here, or Commander is off).");
        return commander;
    }

    void ShowAge(ChatHandler* handler, Commander::WorldModel const& world)
    {
        handler->PSendSysMessage("World model v%u, refreshed %u ms ago, packs %u ms ago",
            world.version,
            WorldTimer::getMSTimeDiffToNow(world.refreshedAtMs),
            WorldTimer::getMSTimeDiffToNow(world.packsRefreshedAtMs));
    }

    // .partybot ai threat
    bool ShowThreat(ChatHandler* handler, Player* player)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        Commander::WorldModel const& world = commander->GetWorld();
        Map* map = player->GetMap();
        ShowAge(handler, world);
        if (world.enemies.empty())
        {
            handler->SendSysMessage("No enemies.");
            return true;
        }

        uint32 shown = 0;
        for (Commander::EnemyFacts const& e : world.enemies)
        {
            if (++shown > 8)
            {
                handler->PSendSysMessage("... %u more", uint32(world.enemies.size() - 8));
                break;
            }

            ObjectGuid closest;
            float const lead = Commander::ThreatView::VictimLead(e, &closest);
            handler->PSendSysMessage("|cffffcc00%s|r %.0f%%%s%s -> %s (%s) for %.1fs, lead %s (%s)",
                NameOf(map, e.guid).c_str(), e.healthPct,
                e.crowdControlled ? " [CC]" : "",
                e.castingSpellId ? " [casting]" : "",
                NameOf(map, e.victim).c_str(), Num(e.victimThreat).c_str(),
                WorldTimer::getMSTimeDiffToNow(e.victimSinceMs) / 1000.0f,
                Num(lead).c_str(), NameOf(map, closest).c_str());

            for (Commander::ThreatEntry const& t : e.threat)
            {
                if (!t.partySide)
                    continue;
                handler->PSendSysMessage("    %s: %s (%s/s)%s, %s, headroom %s",
                    NameOf(map, t.guid).c_str(), Num(t.threat).c_str(), Num(t.threatPerSec).c_str(),
                    t.onList ? "" : " (not on list)",
                    t.inMeleeReach ? "melee 110%" : "ranged 130%",
                    Num(Commander::ThreatView::Headroom(e, t.guid)).c_str());
            }
        }
        return true;
    }

    // .partybot ai threat watch [off|target|all]   (no argument = target)
    // Replies PBOK;;threatwatch;<mode> for the addon's threat window.
    bool SetThreatWatch(ChatHandler* handler, Player* player, char const* arg)
    {
        Commander::ThreatWatch mode = Commander::ThreatWatch::Target;
        if (arg && !strcmp(arg, "off"))
            mode = Commander::ThreatWatch::Off;
        else if (arg && !strcmp(arg, "all"))
            mode = Commander::ThreatWatch::All;
        else if (arg && strcmp(arg, "target"))
        {
            handler->SendSysMessage("Usage: .partybot ai threat watch [off|target|all]");
            return false;
        }
        sCommanderMgr.SetThreatWatch(player->GetObjectGuid(), mode);
        handler->PSendSysMessage("PBOK;;threatwatch;%s", Commander::ThreatWatchName(mode));
        return true;
    }

    // .partybot ai pullmethod [auto|ranged|melee]   (no argument = show)
    // Replies PBOK;;pullmethod;<method> for the addon's pull method button.
    bool SetPullMethod(ChatHandler* handler, Player* player, char const* arg)
    {
        Commander::PullMethod method = sCommanderMgr.GetPullMethod(player->GetObjectGuid());
        if (arg && !Commander::ParsePullMethod(arg, method))
        {
            handler->SendSysMessage("Usage: .partybot ai pullmethod [auto|ranged|face|melee]");
            return false;
        }
        if (arg)
            sCommanderMgr.SetPullMethod(player->GetObjectGuid(), method);
        // Indexed by PullMethod.
        static char const* const meaning[] = {
            "the tank decides (walks in; shoots or face pulls when that brings fewer or the pack is brought back)",
            "the tank shoots whenever it has a ranged pull",
            "the tank always walks in",
            "the tank stands where the target sees it and lets it come",
        };
        handler->PSendSysMessage("Pull method: %s, %s.", Commander::PullMethodName(method), meaning[uint8(method)]);
        handler->PSendSysMessage("PBOK;;pullmethod;%s", Commander::PullMethodName(method));
        return true;
    }

    // .partybot ai pullplace [auto|corner]   (no argument = show)
    // Replies PBOK;;pullplace;<place> for the addon's fight place button.
    bool SetPullPlace(ChatHandler* handler, Player* player, char const* arg)
    {
        Commander::PullPlace place = sCommanderMgr.GetPullPlace(player->GetObjectGuid());
        if (arg && !Commander::ParsePullPlace(arg, place))
        {
            handler->SendSysMessage("Usage: .partybot ai pullplace [auto|corner]");
            return false;
        }
        if (arg)
            sCommanderMgr.SetPullPlace(player->GetObjectGuid(), place);
        handler->PSendSysMessage("Fight place: %s, %s.", Commander::PullPlaceName(place),
            place == Commander::PullPlace::Corner
                ? "the tank always takes the pack round a corner, out of the pulled enemies' sight"
                : "where the pack stands, brought back, or round a corner when the fight looks hard");
        handler->PSendSysMessage("PBOK;;pullplace;%s", Commander::PullPlaceName(place));
        return true;
    }

    char const* PullPhaseName(Commander::PullPhase phase)
    {
        switch (phase)
        {
            case Commander::PullPhase::None:      return "none";
            case Commander::PullPhase::WaitReady: return "waiting for ready";
            case Commander::PullPhase::Approach:  return "tank approaching";
            case Commander::PullPhase::Return:    return "tank returning";
            case Commander::PullPhase::Settle:    return "tank building threat";
        }
        return "?";
    }

    std::string SpellName(uint32 spellId)
    {
        if (!spellId)
            return "-";
        if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId))
            return spell->SpellName[0];
        return std::to_string(spellId);
    }

    // .partybot ai plan
    bool ShowPlan(ChatHandler* handler, Player* player)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        Commander::WorldModel const& world = commander->GetWorld();
        Commander::TeamPlan const& plan = commander->GetPlan();
        Map* map = player->GetMap();
        handler->PSendSysMessage("Team plan v%u", plan.version);

        // Roles (Perception/Tanks.h, RoleAssigner): what each member plays and can do.
        handler->SendSysMessage("Roles (tank / heal / melee / ranged):");
        for (Commander::PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            static char const* const names[] = { "auto", "melee", "ranged", "tank", "healer" };
            char const* role = m.role < sizeof(names) / sizeof(names[0]) ? names[m.role] : "?";
            handler->PSendSysMessage("    %s: %s%s%s  %.2f / %.2f / %.2f / %.2f", NameOf(map, m.guid).c_str(), role,
                m.roleLocked ? " (locked)" : "", m.isBot ? "" : " (player)", m.cap.tank, m.cap.heal, m.cap.melee, m.cap.ranged);
        }

        handler->SendSysMessage("Interrupters:");
        for (Commander::PartyMemberFacts const& m : world.party)
        {
            if (m.interrupts.empty())
                continue;
            std::string list;
            for (Commander::InterruptAbility const& a : m.interrupts)
            {
                if (!list.empty())
                    list += ", ";
                list += SpellName(a.spellId) + (a.ready ? "" : " (cd)");
            }
            handler->PSendSysMessage("    %s: %s", NameOf(map, m.guid).c_str(), list.c_str());
        }

        if (plan.pull.phase != Commander::PullPhase::None)
            handler->PSendSysMessage("Pull: %s, %s -> %s, %u expected, phase for %.1fs",
                plan.pull.awaitArrival ? "tank waiting for the pack" : PullPhaseName(plan.pull.phase),
                NameOf(map, plan.pull.tank).c_str(), NameOf(map, plan.pull.target).c_str(),
                uint32(plan.pull.expected.size()), WorldTimer::getMSTimeDiffToNow(plan.pull.phaseSinceMs) / 1000.0f);
        else
            handler->SendSysMessage("Pull: none");
        if (plan.clear.active)
            handler->PSendSysMessage("Clear: running, %.0f yd, max %u mobs, %u packs done, %u skipped",
                plan.clear.radius, plan.clear.maxMobs, plan.clear.cleared, plan.clear.skippedPacks);
        if (plan.retreat.active)
            handler->PSendSysMessage("Retreat: to (%.1f, %.1f, %.1f), %.0f yd from you, for %.1fs, %u enemies left",
                plan.retreat.point.x, plan.retreat.point.y, plan.retreat.point.z,
                player->GetDistance(plan.retreat.point.x, plan.retreat.point.y, plan.retreat.point.z),
                WorldTimer::getMSTimeDiffToNow(plan.retreat.startedMs) / 1000.0f, uint32(world.enemies.size()));

        // Focus and intercepts (FocusAssigner). Score = harm rate / kill time.
        if (plan.focus.enemy.IsEmpty())
            handler->SendSysMessage("Focus: none");
        else
            handler->PSendSysMessage("Focus: |cffffcc00%s|r score %.3f (%.2f pt/s, kill %.1fs) for %.1fs: %s",
                NameOf(map, plan.focus.enemy).c_str(), plan.focus.score, plan.focus.harmRate, plan.focus.killSec,
                WorldTimer::getMSTimeDiffToNow(plan.focus.sinceMs) / 1000.0f, plan.focus.reason.c_str());
        // Every bot's target (TargetAssigner).
        if (!plan.targets.empty())
        {
            handler->SendSysMessage("Targets:");
            for (Commander::TargetDuty const& d : plan.targets)
            {
                Commander::PartyMemberFacts const* m = world.FindMember(d.bot);
                bool const already = m && m->victim == d.enemy;
                if (d.forced)
                    handler->PSendSysMessage("    %s -> |cffffcc00%s|r (%s, must)%s", NameOf(map, d.bot).c_str(),
                        NameOf(map, d.enemy).c_str(), d.reason, already ? "" : " switching");
                else
                    handler->PSendSysMessage("    %s -> |cffffcc00%s|r (%s, %.1f - %.1f pt%s) for %.1fs%s",
                        NameOf(map, d.bot).c_str(), NameOf(map, d.enemy).c_str(), d.reason, d.benefit, d.cost,
                        d.urgent ? ", urgent" : "", WorldTimer::getMSTimeDiffToNow(d.sinceMs) / 1000.0f,
                        already ? "" : " switching");
            }
        }
        if (!plan.orders.attack.IsEmpty())
            handler->PSendSysMessage("Order: attack |cffffcc00%s|r%s", NameOf(map, plan.orders.attack).c_str(),
                plan.orders.attackTanksOnly ? " (tanks)" : "");
        for (ObjectGuid const& g : plan.orders.leaveAlone)
            handler->PSendSysMessage("Order: leave |cffffcc00%s|r alone", NameOf(map, g).c_str());
        if (!plan.intercepts.empty())
        {
            handler->SendSysMessage("Intercepts:");
            for (Commander::InterceptDuty const& d : plan.intercepts)
            {
                handler->PSendSysMessage("    |cffffcc00%s|r: %s (%s, %.1f pt)",
                    NameOf(map, d.enemy).c_str(), NameOf(map, d.bot).c_str(), d.reason, d.points);
                for (Commander::DutyResult const& r : plan.duties)
                    if (r.kind == Commander::DutyKind::Intercept && r.target == d.enemy)
                        for (Commander::DutyPass const& pass : r.passed)
                            handler->PSendSysMessage("        passed over %s: primary %s on %s",
                                NameOf(map, pass.bot).c_str(), Commander::DutyAllocator::KindName(pass.busyKind),
                                NameOf(map, pass.busyTarget).c_str());
            }
        }

        if (!plan.dispels.empty())
        {
            handler->SendSysMessage("Dispels:");
            for (Commander::DispelDuty const& d : plan.dispels)
                handler->PSendSysMessage("    %s: %s from |cffffcc00%s|r with %s (%.1f pt%s)",
                    NameOf(map, d.bot).c_str(), SpellName(d.auraSpellId).c_str(), NameOf(map, d.target).c_str(),
                    SpellName(d.spellId).c_str(), d.points, d.control ? ", cannot act" : "");
        }

        // Crowd control (CrowdControlAssigner); values per option: .partybot ai control
        if (!plan.controls.empty())
        {
            handler->SendSysMessage("Controls:");
            for (Commander::ControlDuty const& d : plan.controls)
            {
                handler->PSendSysMessage("    %s: %s on |cffffcc00%s|r (%.1f pt%s)",
                    NameOf(map, d.bot).c_str(), SpellName(d.spellId).c_str(), NameOf(map, d.enemy).c_str(),
                    d.points, d.area ? ", area" : "");
                for (Commander::DutyResult const& r : plan.duties)
                    if (r.kind == Commander::DutyKind::CrowdControl && r.target == d.enemy)
                        for (Commander::DutyPass const& pass : r.passed)
                            handler->PSendSysMessage("        passed over %s: primary %s on %s",
                                NameOf(map, pass.bot).c_str(), Commander::DutyAllocator::KindName(pass.busyKind),
                                NameOf(map, pass.busyTarget).c_str());
            }
        }

        if (plan.interrupts.empty())
        {
            handler->SendSysMessage("No interrupt duties (no enemy has cast an interruptible spell yet).");
            return true;
        }
        handler->SendSysMessage("Interrupt duties:");
        for (Commander::InterruptDuty const& d : plan.interrupts)
        {
            Commander::EnemyFacts const* e = world.FindEnemy(d.enemy);
            std::string cast = "-";
            if (e && e->castingSpellId)
                cast = SpellName(e->castingSpellId) + (e->castInterruptible ? "" : " (not interruptible)");
            else if (e && e->lastCastSpellId)
                cast = "last " + SpellName(e->lastCastSpellId);
            handler->PSendSysMessage("    |cffffcc00%s|r [%s, %s]: %s (%s), backup %s (%s)",
                NameOf(map, d.enemy).c_str(), cast.c_str(),
                Commander::SpellTags::CategoryName(Commander::CastCategory(d.category)),
                NameOf(map, d.primary).c_str(), SpellName(d.primarySpellId).c_str(),
                NameOf(map, d.backup).c_str(), SpellName(d.backupSpellId).c_str());
            // Better-ranked bots the allocator did not make primary (DutyAllocator.h).
            for (Commander::DutyResult const& r : plan.duties)
            {
                if (r.kind != Commander::DutyKind::Interrupt || r.target != d.enemy)
                    continue;
                for (Commander::DutyPass const& pass : r.passed)
                {
                    if (pass.reason == Commander::DutyPassReason::Busy)
                        handler->PSendSysMessage("        passed over %s: primary %s on %s",
                            NameOf(map, pass.bot).c_str(), Commander::DutyAllocator::KindName(pass.busyKind),
                            NameOf(map, pass.busyTarget).c_str());
                    else
                        handler->PSendSysMessage("        passed over %s: backup only (no sight, too far or not in front)",
                            NameOf(map, pass.bot).c_str());
                }
            }
        }
        return true;
    }

    // .partybot ai ready
    bool ShowReady(ChatHandler* handler, Player* player)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;
        Map* map = player->GetMap();
        // Same healer mana gate as the pull that is waiting (from its fight estimate, PullPlanner::Plan).
        Commander::PullPlan const& pull = commander->GetPlan().pull;
        float const healerMana = pull.phase == Commander::PullPhase::WaitReady ? pull.healerManaPct
                                                                               : Commander::PullPlanner::READY_MANA_PCT;
        // The waiting pull may also be held for a patrol (PullPlanner::RecheckBeforeApproach).
        if (pull.phase == Commander::PullPhase::WaitReady && pull.patrolWaitSinceMs)
            handler->PSendSysMessage("Pull held for patrol %s for %.0fs (at most %u s).",
                NameOf(map, pull.patrolWaitingFor).c_str(),
                WorldTimer::getMSTimeDiffToNow(pull.patrolWaitSinceMs) / 1000.0f,
                Commander::PullPlanner::PATROL_WAIT_MAX_MS / 1000);
        auto issues = Commander::PullPlanner::CheckReady(commander->GetWorld(), healerMana);
        if (issues.empty())
        {
            handler->SendSysMessage("Everyone is ready.");
            return true;
        }
        for (auto const& i : issues)
        {
            if (!strcmp(i.what, "hp") || !strcmp(i.what, "mana"))
                handler->PSendSysMessage("    %s: %s %.0f%%", NameOf(map, i.who).c_str(), i.what, i.value);
            else if (!strcmp(i.what, "far"))
                handler->PSendSysMessage("    %s: %.0f yd away", NameOf(map, i.who).c_str(), i.value);
            else
                handler->PSendSysMessage("    %s: %s", NameOf(map, i.who).c_str(), i.what);
        }
        return true;
    }

    // .partybot ai pull [now|cancel]   target = your selection, or the nearest hostile pack
    bool StartPull(ChatHandler* handler, Player* player, char const* option, Creature* selected)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        if (option && !strcmp(option, "cancel"))
        {
            commander->GetPlanMutable().pull = Commander::PullPlan();
            Commander::ClearPlanner::Stop(*commander, "you cancelled the pull.");
            handler->SendSysMessage("Pull cancelled.");
            return true;
        }
        if (option && strcmp(option, "now"))
        {
            handler->SendSysMessage("Usage: .partybot ai pull [now|cancel]");
            return false;
        }
        if (!sCommanderMgr.IsModuleEnabled("pull"))
        {
            handler->SendSysMessage("Module 'pull' is off (.partybot ai module pull on).");
            return false;
        }
        if (commander->GetPlan().retreat.active)
        {
            handler->SendSysMessage("Retreating. '.partybot ai retreat stop' first.");
            return false;
        }
        if (player->IsInCombat())
        {
            handler->SendSysMessage("You are in combat.");
            return false;
        }

        std::vector<std::string> report;
        bool const ok = Commander::PullPlanner::Plan(player, *commander, selected, option != nullptr, report);
        for (std::string const& line : report)
            handler->SendSysMessage(line.c_str());
        return ok;
    }

    // .partybot ai clear [radius] [maxmobs] | stop
    bool StartClear(ChatHandler* handler, Player* player, char const* first, char const* second)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        if (first && !strcmp(first, "stop"))
        {
            Commander::TeamPlan& plan = commander->GetPlanMutable();
            plan.pull = Commander::PullPlan();
            if (plan.clear.active)
                Commander::ClearPlanner::Stop(*commander, "you stopped it.");
            else
                handler->SendSysMessage("Clear is not running.");
            return true;
        }
        if (!sCommanderMgr.IsModuleEnabled("pull"))
        {
            handler->SendSysMessage("Module 'pull' is off (.partybot ai module pull on).");
            return false;
        }

        if (commander->GetPlan().retreat.active)
        {
            handler->SendSysMessage("Retreating. '.partybot ai retreat stop' first.");
            return false;
        }

        float radius = 40.0f;
        uint32 maxMobs = 4;
        if (first)
            radius = float(atof(first));
        if (second)
            maxMobs = uint32(atoi(second));
        if (radius < 10.0f || radius > 50.0f || maxMobs < 1 || maxMobs > 10)
        {
            handler->SendSysMessage("Usage: .partybot ai clear [radius 10-50, default 40] [max mobs per pull 1-10, default 4] | stop");
            return false;
        }

        Commander::ClearPlanner::Start(player, *commander, radius, maxMobs);
        handler->PSendSysMessage("Clearing packs within %.0f yd of here, at most %u mobs per pull "
            "(assistance and aggro range counted; waits up to %u s for patrols to pass). ",
            radius, maxMobs, Commander::PullPlanner::PATROL_WAIT_MAX_MS / 1000);
        handler->PSendSysMessage(
            "Pauses while you are more than %.0f yd away (goes on where you stop); stops when nothing is left, "
            "the tank is gone or someone dies twice. '.partybot ai clear stop' to stop.",
            Commander::ClearPlanner::MAX_LEADER_DRIFT);
        return true;
    }

    // .partybot ai retreat [stop]
    // Only the player starts a retreat: the player is not controlled by us, bots running on their
    // own would leave the player to die alone (combat scenario 11, T+18s).
    bool StartRetreat(ChatHandler* handler, Player* player, char const* option)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        if (option && !strcmp(option, "stop"))
        {
            if (!commander->GetPlan().retreat.active)
            {
                handler->SendSysMessage("No retreat is running.");
                return true;
            }
            Commander::RetreatPlanner::Stop(player, *commander, "you stopped it. Bots fight normally again.");
            handler->SendSysMessage("Retreat stopped.");
            return true;
        }
        if (option)
        {
            handler->SendSysMessage("Usage: .partybot ai retreat [stop]");
            return false;
        }
        if (!sCommanderMgr.IsModuleEnabled("retreat"))
        {
            handler->SendSysMessage("Module 'retreat' is off (.partybot ai module retreat on).");
            return false;
        }

        // Running again plans a new point from where the player is now.
        std::vector<std::string> report;
        bool const ok = Commander::RetreatPlanner::Start(player, *commander, report);
        for (std::string const& line : report)
            handler->SendSysMessage(line.c_str());
        return ok;
    }

    // .partybot ai packs
    bool ShowPacks(ChatHandler* handler, Player* player)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        Commander::WorldModel const& world = commander->GetWorld();
        Map* map = player->GetMap();
        ShowAge(handler, world);
        if (world.packs.empty())
        {
            handler->PSendSysMessage("No out-of-combat packs within %.0f yards.", 50.0f);
            return true;
        }

        uint32 index = 0;
        for (Commander::PackFacts const& pack : world.packs)
        {
            if (++index > 10)
            {
                handler->PSendSysMessage("... %u more", uint32(world.packs.size() - 10));
                break;
            }

            std::string names;
            for (Commander::PackCreature const& c : pack.members)
            {
                if (!names.empty())
                    names += ", ";
                names += NameOf(map, c.guid);
                if (!c.aggressive)
                    names += "(neutral)";
            }
            handler->PSendSysMessage("#%u %.0fyd [%s] %u mobs, %u elite, max lvl %u: %s",
                index, pack.distanceToLeader, PackSourceName(pack.source),
                uint32(pack.members.size()), pack.eliteCount, pack.maxLevel, names.c_str());
        }
        return true;
    }

    // .partybot ai healsim <amount> [healer]   heal target = your selection, or yourself
    bool HealSim(ChatHandler* handler, Player* player, uint32 amount, char const* healerName, Unit* target)
    {
        auto commander = CommanderFor(handler, player);
        if (!commander)
            return false;

        Group* group = player->GetGroup();
        Player* healer = nullptr;
        if (healerName)
        {
            healer = sObjectMgr.GetPlayer(healerName);
        }
        else
        {
            for (GroupReference* itr = group->GetFirstMember(); itr && !healer; itr = itr->next())
            {
                Player* member = itr->getSource();
                if (member && member->IsInWorld() && member->GetMap() == player->GetMap() && member->AI())
                    if (PartyBotAI const* ai = dynamic_cast<PartyBotAI const*>(member->AI()))
                        if (ai->m_role == ROLE_HEALER)
                            healer = member;
            }
        }
        if (!healer || healer->GetMap() != player->GetMap() || !group->IsMember(healer->GetObjectGuid()))
        {
            handler->SendSysMessage("Healer not found in your party on this map. Give a name or set a bot to healer.");
            return false;
        }

        if (!target)
            target = player;

        float const missing = float(target->GetMaxHealth() - target->GetHealth());
        float const effective = std::min(float(amount), missing);

        // No spell: base 0.5 / 0.25 heal threat with the healer's general threat modifiers.
        std::vector<Commander::ThreatView::EnemyThreatDelta> deltas;
        Commander::ThreatView::PredictHealThreat(healer, target, effective, nullptr, deltas);

        Commander::WorldModel const& world = commander->GetWorld();
        Map* map = player->GetMap();
        handler->PSendSysMessage("%s heals %s for %u (effective %.0f, missing %.0f), split over %u enemies:",
            healer->GetName(), target->GetName(), amount, effective, missing, uint32(deltas.size()));

        for (auto const& d : deltas)
        {
            Commander::EnemyFacts const* e = world.FindEnemy(d.enemy);
            float const before = e ? Commander::ThreatView::Headroom(*e, healer->GetObjectGuid()) : Commander::ThreatView::NO_LIMIT;
            float const after = before - d.added;
            handler->PSendSysMessage("    %s: +%s threat, headroom %s -> %s%s",
                NameOf(map, d.enemy).c_str(), Num(d.added).c_str(), Num(before).c_str(), Num(after).c_str(),
                after < 0.0f ? "  |cffff4040PULLS AGGRO|r" : "");
        }

        ObjectGuid worst;
        float const worstAfter = Commander::ThreatView::WorstHeadroomAfter(world, healer->GetObjectGuid(), deltas, &worst);
        handler->PSendSysMessage("Worst: %s on %s", Num(worstAfter).c_str(), NameOf(map, worst).c_str());
        return true;
    }

    void ShowStats(ChatHandler* handler)
    {
        Commander::TickStats const s = sCommanderMgr.GetStats();
        double const seconds = s.elapsedMs ? s.elapsedMs / 1000.0 : 0.0;
        double const avgUs = s.ticks ? double(s.totalMicros) / s.ticks : 0.0;
        double const ticksPerSec = seconds > 0.0 ? s.ticks / seconds : 0.0;
        // Share of one CPU core spent inside bot AI ticks.
        double const corePercent = s.elapsedMs ? (s.totalMicros / 1000.0) / s.elapsedMs * 100.0 : 0.0;

        handler->PSendSysMessage("Bot AI ticks: %llu in %.1fs (%.1f/s), avg %.1f us, max %llu us, %.3f%% of one core",
            (unsigned long long)s.ticks, seconds, ticksPerSec, avgUs, (unsigned long long)s.maxMicros, corePercent);
    }

    void ShowStatus(ChatHandler* handler)
    {
        uint32 enabledModules = 0;
        for (auto const& slot : sCommanderMgr.GetBehaviors())
            if (slot->enabled.load())
                ++enabledModules;
        for (auto const& slot : sCommanderMgr.GetAssigners())
            if (slot->enabled.load())
                ++enabledModules;

        handler->PSendSysMessage("Commander: %s, bot update %u ms, perception %u ms, %u party commanders, modules %u/%u enabled",
            sCommanderMgr.IsEnabled() ? "on" : "off",
            sCommanderMgr.GetBotUpdateInterval(),
            sCommanderMgr.GetPerceptionInterval(),
            uint32(sCommanderMgr.GetCommanderCount()),
            enabledModules, uint32(sCommanderMgr.GetBehaviors().size() + sCommanderMgr.GetAssigners().size()));
        ShowStats(handler);
    }
}

bool ChatHandler::HandlePartyBotAiCommand(char* args)
{
    char* sub = ExtractLiteralArg(&args);
    if (!sub || !strcmp(sub, "status"))
    {
        ShowStatus(this);
        return true;
    }

    if (!strcmp(sub, "on") || !strcmp(sub, "off"))
    {
        sCommanderMgr.SetEnabled(!strcmp(sub, "on"));
        ShowStatus(this);
        return true;
    }

    if (!strcmp(sub, "interval"))
    {
        uint32 ms = 0;
        if (!ExtractUInt32(&args, ms))
        {
            PSendSysMessage("Bot update interval: %u ms. Usage: .partybot ai interval <100-5000>", sCommanderMgr.GetBotUpdateInterval());
            return true;
        }
        sCommanderMgr.SetBotUpdateInterval(ms);
        sCommanderMgr.ResetStats();
        PSendSysMessage("Bot update interval set to %u ms (not saved). Stats reset.", sCommanderMgr.GetBotUpdateInterval());
        return true;
    }

    if (!strcmp(sub, "stats"))
    {
        char* opt = ExtractLiteralArg(&args);
        if (opt && !strcmp(opt, "reset"))
        {
            sCommanderMgr.ResetStats();
            SendSysMessage("Stats reset.");
            return true;
        }
        ShowStats(this);
        return true;
    }

    if (!strcmp(sub, "modules"))
    {
        for (auto const& slot : sCommanderMgr.GetAssigners())
            PSendSysMessage("  %s (team assignment): %s", slot->module->GetId(), slot->enabled.load() ? "on" : "off");
        for (auto const& slot : sCommanderMgr.GetBehaviors())
            PSendSysMessage("  %s (behavior): %s", slot->module->GetId(), slot->enabled.load() ? "on" : "off");
        return true;
    }

    if (!strcmp(sub, "module"))
    {
        char* id = ExtractLiteralArg(&args);
        char* state = ExtractLiteralArg(&args);
        if (!id || !state || (strcmp(state, "on") && strcmp(state, "off")))
        {
            SendSysMessage("Usage: .partybot ai module <id> on|off");
            SetSentErrorMessage(true);
            return false;
        }
        if (!sCommanderMgr.SetModuleEnabled(id, !strcmp(state, "on")))
        {
            PSendSysMessage("Unknown module '%s'. See .partybot ai modules", id);
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("Module %s: %s", id, state);
        return true;
    }

    if (!strcmp(sub, "reload"))
    {
        char* what = ExtractLiteralArg(&args);
        if (what && !strcmp(what, "overrides"))
        {
            Commander::ProfileCommands::ReloadOverrides(this);
            return true;
        }
        sCommanderMgr.LoadConfig();
        ShowStatus(this);
        return true;
    }

    if (!strcmp(sub, "profile"))
    {
        if (!Commander::ProfileCommands::ShowCreature(this, GetSelectedCreature()))
        {
            SetSentErrorMessage(true);
            return false;
        }
        return true;
    }

    if (!strcmp(sub, "spell"))
    {
        uint32 spellId = 0;
        if (!ExtractUInt32(&args, spellId))
        {
            SendSysMessage("Usage: .partybot ai spell <spell id> [caster level]");
            SetSentErrorMessage(true);
            return false;
        }
        uint32 level = 0;
        if (!ExtractOptUInt32(&args, level, 0) || !level)
            level = GetSelectedCreature() ? GetSelectedCreature()->GetLevel() : m_session->GetPlayer()->GetLevel();
        if (!Commander::ProfileCommands::ShowSpell(this, spellId, level))
        {
            SetSentErrorMessage(true);
            return false;
        }
        return true;
    }

    if (!strcmp(sub, "control"))
    {
        if (!Commander::ProfileCommands::ShowControls(this, m_session->GetPlayer()))
        {
            SetSentErrorMessage(true);
            return false;
        }
        return true;
    }

    if (!strcmp(sub, "rates"))
    {
        if (!Commander::ProfileCommands::ShowRates(this, m_session->GetPlayer()))
        {
            SetSentErrorMessage(true);
            return false;
        }
        return true;
    }

    if (!strcmp(sub, "bot"))
    {
        Player* pBot = GetSelectedPlayer();
        PartyBotAI* pAI = (pBot && pBot->AI()) ? dynamic_cast<PartyBotAI*>(pBot->AI()) : nullptr;
        if (!pAI)
        {
            SendSysMessage("Select a party bot first.");
            SetSentErrorMessage(true);
            return false;
        }

        // Read-only peek from the GM's thread; good enough for a debug command.
        uint32 const now = WorldTimer::getMSTime();
        // const overload: never creates the directives from this thread
        Commander::BotDirectives const& d = static_cast<PartyBotAI const*>(pAI)->Directives();
        PSendSysMessage("%s: movement hold %u ms left", pBot->GetName(), d.GetMovementHoldRemaining(now));
        if (d.GetLastDecision().empty())
            SendSysMessage("  last decision: none");
        else
            PSendSysMessage("  last decision %u ms ago: %s", WorldTimer::getMSTimeDiff(d.GetLastDecisionMs(), now), d.GetLastDecision().c_str());
        uint32 count = 1;
        ExtractOptUInt32(&args, count, 1);
        Commander::ProfileCommands::ShowDecisions(this, d, std::max(1u, std::min(count, uint32(Commander::BotDirectives::LOG_SIZE))));
        return true;
    }

    if (!strcmp(sub, "threat") || !strcmp(sub, "packs") || !strcmp(sub, "healsim") || !strcmp(sub, "plan") ||
        !strcmp(sub, "pull") || !strcmp(sub, "ready") || !strcmp(sub, "clear") || !strcmp(sub, "retreat") ||
        !strcmp(sub, "pullmethod") || !strcmp(sub, "pullplace"))
    {
        Player* player = m_session->GetPlayer();
        bool ok = false;
        if (!strcmp(sub, "pullmethod"))
            ok = SetPullMethod(this, player, ExtractLiteralArg(&args));
        else if (!strcmp(sub, "pullplace"))
            ok = SetPullPlace(this, player, ExtractLiteralArg(&args));
        else if (!strcmp(sub, "threat"))
        {
            char const* arg = ExtractLiteralArg(&args);
            ok = arg && !strcmp(arg, "watch") ? SetThreatWatch(this, player, ExtractLiteralArg(&args))
                                               : ShowThreat(this, player);
        }
        else if (!strcmp(sub, "retreat"))
            ok = StartRetreat(this, player, ExtractLiteralArg(&args));
        else if (!strcmp(sub, "clear"))
        {
            char const* first = ExtractLiteralArg(&args);
            char const* second = ExtractLiteralArg(&args);
            ok = StartClear(this, player, first, second);
        }
        else if (!strcmp(sub, "ready"))
            ok = ShowReady(this, player);
        else if (!strcmp(sub, "pull"))
            ok = StartPull(this, player, ExtractLiteralArg(&args), GetSelectedCreature());
        else if (!strcmp(sub, "plan"))
            ok = ShowPlan(this, player);
        else if (!strcmp(sub, "packs"))
            ok = ShowPacks(this, player);
        else
        {
            uint32 amount = 0;
            if (!ExtractUInt32(&args, amount) || !amount)
            {
                SendSysMessage("Usage: .partybot ai healsim <heal amount> [healer name]   (target = your selection)");
                SetSentErrorMessage(true);
                return false;
            }
            char const* healerName = ExtractLiteralArg(&args);
            ok = HealSim(this, player, amount, healerName, GetSelectedUnit());
        }
        if (!ok)
            SetSentErrorMessage(true);
        return ok;
    }

    SendSysMessage("Usage: .partybot ai [status|on|off|interval <ms>|stats [reset]|modules|module <id> on|off|reload [overrides]|bot [n]|threat [watch off|target|all]|packs|plan|ready|pull [now|cancel]|pullmethod [auto|ranged|face|melee]|pullplace [auto|corner]|clear [radius] [maxmobs]|clear stop|retreat [stop]|healsim <amount> [healer]|profile|spell <id> [level]|rates]");
    SetSentErrorMessage(true);
    return false;
}
