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
 */

#include "Chat.h"
#include "Player.h"
#include "PartyBotAI.h"
#include "Commander/CommanderMgr.h"
#include "Commander/BotDirectives.h"
#include "Timer.h"
#include <cstring>

namespace
{
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

        handler->PSendSysMessage("Commander: %s, bot update %u ms, perception %u ms, %u party commanders, modules %u/%u enabled",
            sCommanderMgr.IsEnabled() ? "on" : "off",
            sCommanderMgr.GetBotUpdateInterval(),
            sCommanderMgr.GetPerceptionInterval(),
            uint32(sCommanderMgr.GetCommanderCount()),
            enabledModules, uint32(sCommanderMgr.GetBehaviors().size()));
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
        if (sCommanderMgr.GetBehaviors().empty())
            SendSysMessage("No behavior modules registered.");
        for (auto const& slot : sCommanderMgr.GetBehaviors())
            PSendSysMessage("  %s: %s", slot->behavior->GetId(), slot->enabled.load() ? "on" : "off");
        return true;
    }

    if (!strcmp(sub, "module"))
    {
        char* id = ExtractLiteralArg(&args);
        char* state = ExtractLiteralArg(&args);
        Commander::BehaviorSlot* slot = nullptr;
        if (!id || !state || (strcmp(state, "on") && strcmp(state, "off")))
        {
            SendSysMessage("Usage: .partybot ai module <id> on|off");
            SetSentErrorMessage(true);
            return false;
        }
        if (!sCommanderMgr.FindBehavior(id, &slot))
        {
            PSendSysMessage("Unknown module '%s'. See .partybot ai modules", id);
            SetSentErrorMessage(true);
            return false;
        }
        slot->enabled.store(!strcmp(state, "on"));
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

    SendSysMessage("Usage: .partybot ai [status|on|off|interval <ms>|stats [reset]|modules|module <id> on|off|reload|bot]");
    SetSentErrorMessage(true);
    return false;
}
