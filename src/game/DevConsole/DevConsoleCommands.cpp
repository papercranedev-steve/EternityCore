/*
 *   .lua <code>       console only (mangosd console / SOAP): run Lua, print one JSON line
 *   .lua reload       re-read DevConsole.* config, recreate the Lua state, run init.lua
 *   .scenario [args]  player command: calls scenario.command(player, args) in Lua.
 *                     Only for the account in DevConsole.ScenarioAccount (it changes level and gear).
 */

#include "Chat.h"
#include "Player.h"
#include "WorldSession.h"
#include "DevConsole/DevConsole.h"
#include "DevConsole/LuaJson.h"
#include "Util.h"
#include <cstring>

bool ChatHandler::HandleLuaCommand(char* args)
{
    if (m_session)
    {
        SendSysMessage(".lua only runs from the server console or SOAP.");
        SetSentErrorMessage(true);
        return false;
    }

    if (!sDevConsole.IsEnabled())
    {
        SendSysMessage("{\"ok\":false,\"error\":\"DevConsole.Enable is 0\"}");
        return true;
    }

    std::string code = args ? args : "";
    // trim
    size_t b = code.find_first_not_of(" \t\r\n");
    size_t e = code.find_last_not_of(" \t\r\n");
    code = (b == std::string::npos) ? std::string() : code.substr(b, e - b + 1);

    if (code == "reload")
    {
        sDevConsole.LoadConfig();
        std::string err = sDevConsole.Reload();
        std::string out = err.empty() ? "{\"ok\":true,\"result\":\"reloaded\"}" : "{\"ok\":false,\"error\":";
        if (!err.empty())
        {
            DevConsole::JsonEscape(err.data(), err.size(), out);
            out += "}";
        }
        SendSysMessage(out.c_str());
        return true;
    }

    if (code.empty())
    {
        SendSysMessage("{\"ok\":false,\"error\":\"usage: lua <code> | lua reload\"}");
        return true;
    }

    std::string out = sDevConsole.Run(code);
    SendSysMessage(out.c_str());
    return true;
}

bool ChatHandler::HandleScenarioCommand(char* args)
{
    Player* pPlayer = m_session ? m_session->GetPlayer() : nullptr;
    if (!pPlayer)
        return false;

    if (!sDevConsole.IsEnabled())
    {
        SendSysMessage("Dev console is disabled (DevConsole.Enable = 0).");
        SetSentErrorMessage(true);
        return false;
    }

    if (!sDevConsole.IsScenarioAccount(m_session->GetUsername()))
    {
        PSendSysMessage("PBERR;;scenario;Scenarios change level and gear, only account %s may use them.",
            sDevConsole.GetScenarioAccount().c_str());
        SetSentErrorMessage(true);
        return false;
    }

    std::string err = sDevConsole.CallPlayerHook("scenario.command", pPlayer, args ? args : "");
    if (!err.empty())
    {
        // first line only, the traceback goes to Server.log
        std::string first = err.substr(0, err.find('\n'));
        PSendSysMessage("PBERR;;scenario;%s", first.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}
