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
 *   .partybot ai bot                 state of the selected bot
 *   .partybot ai threat              enemies of your party: victim, threat, headroom per member
 *   .partybot ai packs               out-of-combat packs within 50 yards
 *   .partybot ai plan                team plan: pull state, interrupt abilities and duties
 *   .partybot ai ready               who is not ready to pull (hp, mana, distance, dead)
 *   .partybot ai pull [now|cancel]   bots plan and pull your selection's pack (or the nearest
 *                                    hostile pack) once everyone is ready; "now" skips the wait
 *   .partybot ai clear [r] [n]       pull every pack within r yards one after another (at most
 *                                    n mobs per pull), waiting until ready in between; "clear stop"
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
#include "Creature.h"
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
                PullPhaseName(plan.pull.phase), NameOf(map, plan.pull.tank).c_str(), NameOf(map, plan.pull.target).c_str(),
                uint32(plan.pull.expected.size()), WorldTimer::getMSTimeDiffToNow(plan.pull.phaseSinceMs) / 1000.0f);
        else
            handler->SendSysMessage("Pull: none");
        if (plan.clear.active)
            handler->PSendSysMessage("Clear: running, %.0f yd, max %u mobs, %u packs done, %u skipped",
                plan.clear.radius, plan.clear.maxMobs, plan.clear.cleared, plan.clear.skippedPacks);

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
        auto issues = Commander::PullPlanner::CheckReady(commander->GetWorld());
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
        handler->PSendSysMessage("Clearing packs within %.0f yd of here, at most %u mobs per pull. "
            "Stops when nothing is left, someone dies, or you move more than %.0f yd. '.partybot ai clear stop' to stop.",
            radius, maxMobs, Commander::ClearPlanner::MAX_LEADER_DRIFT);
        return true;
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
        sCommanderMgr.LoadConfig();
        ShowStatus(this);
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
        return true;
    }

    if (!strcmp(sub, "threat") || !strcmp(sub, "packs") || !strcmp(sub, "healsim") || !strcmp(sub, "plan") ||
        !strcmp(sub, "pull") || !strcmp(sub, "ready") || !strcmp(sub, "clear"))
    {
        Player* player = m_session->GetPlayer();
        bool ok = false;
        if (!strcmp(sub, "threat"))
            ok = ShowThreat(this, player);
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

    SendSysMessage("Usage: .partybot ai [status|on|off|interval <ms>|stats [reset]|modules|module <id> on|off|reload|bot|threat|packs|plan|ready|pull [now|cancel]|clear [radius] [maxmobs]|clear stop|healsim <amount> [healer]]");
    SetSentErrorMessage(true);
    return false;
}
