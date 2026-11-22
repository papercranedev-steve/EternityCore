/*
 * .partybot voice [quiet | normal | chatty] [marks on | off]   (VoiceSettings.h)
 *   What the player wants to hear from his party: how talkative it is, and whether the bots put
 *   raid target marks on what they crowd-control. The PartyBot addon's settings send it (again
 *   after login: the server keeps it until restart only).
 *   -> PBOK;;voice;<quiet|normal|chatty>;<on|off>        the settings now
 *   -> PBERR;;usage;.partybot voice [quiet|normal|chatty] [marks on|off]
 */

#include "Commander/CommanderMgr.h"
#include "Commander/Commands/Voice/VoiceSettings.h"
#include "Chat.h"
#include "Player.h"
#include "WorldSession.h"
#include <cstring>

bool ChatHandler::HandlePartyBotVoiceCommand(char* args)
{
    using namespace Commander;
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player)
        return false;

    VoiceSettings settings = sCommanderMgr.GetVoiceSettings(player->GetObjectGuid());
    while (char* arg = ExtractLiteralArg(&args))
    {
        VoiceLevel level;
        if (ParseVoiceLevel(arg, level))
        {
            settings.level = level;
            continue;
        }
        char* state = !strcmp(arg, "marks") ? ExtractLiteralArg(&args) : nullptr;
        if (!state || (strcmp(state, "on") && strcmp(state, "off")))
        {
            SendSysMessage("PBERR;;usage;.partybot voice [quiet|normal|chatty] [marks on|off]");
            SetSentErrorMessage(true);
            return false;
        }
        settings.marks = !strcmp(state, "on");
    }
    sCommanderMgr.SetVoiceSettings(player->GetObjectGuid(), settings);
    PSendSysMessage("PBOK;;voice;%s;%s", VoiceLevelName(settings.level), settings.marks ? "on" : "off");
    return true;
}
