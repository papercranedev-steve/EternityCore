/*
 * Companions: chat commands (soul companions doc §5).
 *
 *   .partybot reply <id> <option>             the player's answer to a bot's request (the
 *                                             PartyBot addon's popup sends it)
 *   .partybot rewardmode <name> auto|ask      who picks the companion's quest rewards
 *   .partybot gate add [name]                 a town gate where the player stands (companions
 *                 remove                      wait there after a town trip, Shared/TownGates)
 *                 list                        (remove: the nearest within 30 yd; list: this zone)
 *   .partybot mirror show <id>                dev: a mirror quest in your own quest log
 *                    count <id> <objective> <n>    (Shared/QuestMirror; objective 1..4)
 *                    remove <id>
 */

#include "Chat.h"
#include "Player.h"
#include "Companions/CompanionMgr.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/QuestMirror.h"
#include "Shared/TownGates.h"
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

bool ChatHandler::HandlePartyBotGateCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    char* action = ExtractLiteralArg(&args);
    if (!player || !action)
    {
        SendSysMessage("PBERR;;usage;.partybot gate add [name] | remove | list");
        SetSentErrorMessage(true);
        return false;
    }

    if (strcmp(action, "add") == 0)
    {
        std::string name = args ? args : "";
        while (!name.empty() && name.front() == ' ')
            name.erase(name.begin());
        uint32 const id = BotShared::TownGates::Add(player->GetMapId(), player->GetZoneId(),
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), name);
        if (!id)
        {
            SendSysMessage("PBERR;;other");
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;gate;add;%u;%s", id, name.c_str());
        return true;
    }

    if (strcmp(action, "remove") == 0)
    {
        BotShared::TownGates::Gate removed;
        if (!BotShared::TownGates::RemoveNear(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), 30.0f, removed))
        {
            SendSysMessage("PBERR;;notfound");
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;gate;remove;%u;%s", removed.id, removed.name.c_str());
        return true;
    }

    if (strcmp(action, "list") == 0)
    {
        std::vector<BotShared::TownGates::Gate> const gates = BotShared::TownGates::OfZone(player->GetMapId(), player->GetZoneId());
        PSendSysMessage("Town gates in this zone: %u", uint32(gates.size()));
        for (BotShared::TownGates::Gate const& g : gates)
            PSendSysMessage("  #%u %s (%.1f, %.1f, %.1f), %.0f yd", g.id, g.name.c_str(), g.x, g.y, g.z,
                player->GetDistance2d(g.x, g.y));
        return true;
    }

    SendSysMessage("PBERR;;usage;.partybot gate add [name] | remove | list");
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandlePartyBotMirrorCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    char* action = ExtractLiteralArg(&args);
    uint32 id = 0;
    if (!player || !action || !ExtractUInt32(&args, id))
    {
        SendSysMessage("PBERR;;usage;.partybot mirror show <id> | count <id> <objective 1-4> <n> | remove <id>");
        SetSentErrorMessage(true);
        return false;
    }

    namespace Mirror = BotShared::QuestMirror;
    if (strcmp(action, "show") == 0)
    {
        switch (Mirror::Show(player, id))
        {
            case Mirror::ShowResult::Shown:        PSendSysMessage("PBOK;;mirror;show;%u", id); return true;
            case Mirror::ShowResult::AlreadyShown: PSendSysMessage("PBOK;;mirror;already;%u", id); return true;
            case Mirror::ShowResult::LogFull:      PSendSysMessage("PBERR;;logfull;%u", id); break;
            case Mirror::ShowResult::NoQuest:      PSendSysMessage("PBERR;;notfound;%u", id); break;
        }
        SetSentErrorMessage(true);
        return false;
    }

    if (strcmp(action, "count") == 0)
    {
        uint32 objective = 0, count = 0;
        if (!ExtractUInt32(&args, objective) || !ExtractUInt32(&args, count) || objective < 1 ||
            !Mirror::SetCount(player, id, objective - 1, count))
        {
            PSendSysMessage("PBERR;;mirror;count;%u", id);
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;mirror;count;%u;%u;%u", id, objective, count);
        return true;
    }

    if (strcmp(action, "remove") == 0)
    {
        if (!Mirror::Remove(player, id))
        {
            PSendSysMessage("PBERR;;notfound;%u", id);
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;mirror;remove;%u", id);
        return true;
    }

    SendSysMessage("PBERR;;usage;.partybot mirror show <id> | count <id> <objective 1-4> <n> | remove <id>");
    SetSentErrorMessage(true);
    return false;
}
