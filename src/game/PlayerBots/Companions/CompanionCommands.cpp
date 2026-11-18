/*
 * Companions: chat commands (soul companions doc §5).
 *
 *   .partybot reply <id> <option>             the player's answer to a bot's request (the
 *                                             PartyBot addon's popup sends it)
 *   .partybot rewardmode <name> auto|ask      who picks the companion's quest rewards
 */

#include "Chat.h"
#include "Player.h"
#include "Companions/CompanionMgr.h"
#include "Life/Requests/RequestBoard.h"
#include "WorldSession.h"
#include <cstring>

bool ChatHandler::HandlePartyBotReplyCommand(char* args)
{
    uint32 id = 0;
    char* option = nullptr;
    if (!ExtractUInt32(&args, id) || !(option = ExtractLiteralArg(&args)))
    {
        SendSysMessage("PBERR;;usage;.partybot reply <id> <option>");
        SetSentErrorMessage(true);
        return false;
    }

    std::string error;
    if (!sRequestBoard.Reply(GetSession() ? GetSession()->GetPlayer() : nullptr, id, option, error))
    {
        PSendSysMessage("PBERR;;%s;%u", error.c_str(), id);
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;;reply;%u;%s", id, option);
    return true;
}

bool ChatHandler::HandlePartyBotRewardModeCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot)
        return false;

    if (!sCompanionMgr.IsCompanion(pBot->GetGUIDLow()))
    {
        PSendSysMessage("PBERR;%s;notcompanion", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    char* mode = ExtractLiteralArg(&args);
    Companions::RewardMode value;
    if (mode && strcmp(mode, "auto") == 0)
        value = Companions::RewardMode::Auto;
    else if (mode && strcmp(mode, "ask") == 0)
        value = Companions::RewardMode::AskPlayer;
    else
    {
        PSendSysMessage("PBERR;%s;usage;.partybot rewardmode <name> auto|ask", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    sCompanionMgr.SetRewardMode(pBot->GetGUIDLow(), value);
    PSendSysMessage("PBOK;%s;rewardmode;%s", name.c_str(), mode);
    return true;
}
