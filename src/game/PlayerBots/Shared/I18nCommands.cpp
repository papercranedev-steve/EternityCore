/*
 * .partybot language [zh | en | reload] (Shared/I18n.h, Shared/Phrases.h)
 *   no argument  -> PBOK;;language;<code>              the account's language now
 *   zh | en      -> PBOK;;language;<code>              the account reads that from now on (the
 *                                                      PartyBot addon sends it at login)
 *   reload       -> PBOK;;language;reload;<1 | 0>      read the language files again (0: one could
 *                                                      not be read, see the server log)
 *   else         -> PBERR;;usage;.partybot language [zh|en|reload]
 */

#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "Chat.h"
#include "Player.h"
#include "WorldSession.h"
#include <cstring>

bool ChatHandler::HandlePartyBotLanguageCommand(char* args)
{
    namespace I18n = BotShared::I18n;
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    char* arg = ExtractLiteralArg(&args);

    if (arg && strcmp(arg, "reload") == 0)
    {
        bool const ok = BotShared::Phrases::Load();
        PSendSysMessage("PBOK;;language;reload;%u", ok ? 1u : 0u);
        return true;
    }
    if (!player)
    {
        SendSysMessage("PBERR;;usage;.partybot language [zh|en|reload]");
        SetSentErrorMessage(true);
        return false;
    }
    if (arg)
    {
        I18n::Lang lang;
        if (!I18n::Parse(arg, lang))
        {
            SendSysMessage("PBERR;;usage;.partybot language [zh|en|reload]");
            SetSentErrorMessage(true);
            return false;
        }
        I18n::Set(session->GetAccountId(), lang);
    }
    PSendSysMessage("PBOK;;language;%s", I18n::Code(I18n::Of(player)));
    return true;
}
