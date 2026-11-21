/*
 * Companions: talking to a party bot and the stay / follow orders (design
 * .kiro/docs/2026-11-21-companion-talk.md §1, §4, §6; workspace root). The PartyBot addon's talk
 * window sends them.
 *
 *   .partybot talk <bot>                  open the talk window: he turns to you and greets you
 *     PBTALK;<bot>;BEGIN;<race:gender:class>;<his words>
 *     PBTALK;<bot>;OPT;<option id>;<icon key>;<price copper, 0 = none>;<your words>
 *     PBTALK;<bot>;END
 *     PBTALK;<bot>;BUSY;<his words>        he or you fight (he says it aloud too)
 *     PBERR;<bot>;far | notgroup | dead
 *   .partybot talk <bot> <option id>      he answers aloud, then does it:
 *     a new page (BEGIN .. END as above), PBTALK;<bot>;CLOSE, or
 *     PBTALK;<bot>;OPEN;<window>;<arg>     the addon opens that window (bags, craft)
 *   .partybot stay [bot]                  he (no name: every party bot of your group) waits here
 *   .partybot follow [bot]                he (no name: everyone waiting) follows you again
 *     PBOK;<bot or empty>;stay|follow;<how many>   (one of them says so in party chat)
 */

#include "Chat.h"
#include "Player.h"
#include "Group.h"
#include "Companions/Talk/Talk.h"
#include "Commander/CommanderMgr.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "PartyBotAI.h"
#include "WorldSession.h"
#include "Utilities/Random.h"
#include <sstream>

namespace
{
    void SendPage(ChatHandler& handler, Player* bot, Companions::Talk::Page const& page)
    {
        std::string const name = bot->GetName();
        std::ostringstream begin;
        begin << "PBTALK;" << name << ";BEGIN;" << uint32(bot->GetRace()) << ":" << uint32(bot->GetGender()) << ":"
              << uint32(bot->GetClass()) << ";" << page.text;
        handler.SendSysMessage(begin.str().c_str());
        for (Companions::Talk::Option const& option : page.options)
        {
            std::ostringstream line;
            line << "PBTALK;" << name << ";OPT;" << option.id << ";" << Companions::Talk::IconKey(option.icon) << ";"
                 << option.price << ";" << option.text;
            handler.SendSysMessage(line.str().c_str());
        }
        handler.PSendSysMessage("PBTALK;%s;END", name.c_str());
    }

    // Stay or follow for one member; true if it changed.
    bool Order(Player* bot, bool stay)
    {
        return bot && bot->AI() && dynamic_cast<PartyBotAI*>(bot->AI()) && sCommanderMgr.SetStay(bot, stay);
    }

    std::string SayLine(Player* player, Player* bot, std::string const& key)
    {
        return Companions::Talk::Line(Companions::Talk::ContextOf(player, bot), key);
    }
}

bool ChatHandler::HandlePartyBotTalkCommand(char* args)
{
    namespace Talk = Companions::Talk;
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!player)
        return false;

    std::string name;
    char* botArg = ExtractArg(&args);
    Player* bot = ExtractPartyBotTarget(&botArg, name);
    if (!bot)
        return false;
    char* optionArg = ExtractArg(&args);

    std::string busyLine;
    switch (Talk::Check(player, bot, busyLine))
    {
        case Talk::Refusal::None:
            break;
        case Talk::Refusal::Busy:
            BotShared::BotChat::Say(bot, busyLine);
            PSendSysMessage("PBTALK;%s;BUSY;%s", name.c_str(), busyLine.c_str());
            return true;
        case Talk::Refusal::Far:
            PSendSysMessage("PBERR;%s;far", name.c_str());
            SetSentErrorMessage(true);
            return false;
        case Talk::Refusal::Dead:
            PSendSysMessage("PBERR;%s;dead", name.c_str());
            SetSentErrorMessage(true);
            return false;
        case Talk::Refusal::NotInGroup:
        default:
            PSendSysMessage("PBERR;%s;notgroup", name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    Talk::Context const ctx = Talk::ContextOf(player, bot);
    if (!optionArg)
    {
        SendPage(*this, bot, Talk::Open(ctx));
        return true;
    }

    Talk::Outcome outcome;
    if (!Talk::Choose(ctx, optionArg, outcome))
    {
        // An old window (what he offers changed): the page as it is now.
        Talk::Page page;
        page.text = outcome.reply;
        page.options = Talk::Offers(ctx);
        SendPage(*this, bot, page);
        return true;
    }

    BotShared::BotChat::Say(bot, outcome.reply);
    switch (outcome.kind)
    {
        case Talk::Outcome::Kind::Page:
        {
            Talk::Page page;
            page.text = outcome.text;
            page.options = outcome.options;
            SendPage(*this, bot, page);
            break;
        }
        case Talk::Outcome::Kind::Open:
            PSendSysMessage("PBTALK;%s;OPEN;%s;%s", name.c_str(), outcome.window.c_str(), outcome.arg.c_str());
            break;
        case Talk::Outcome::Kind::Close:
        default:
            PSendSysMessage("PBTALK;%s;CLOSE", name.c_str());
            break;
    }
    return true;
}

bool ChatHandler::HandlePartyBotStayHelper(char* args, bool stay)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!player)
        return false;
    char const* const what = stay ? "stay" : "follow";

    // One bot by name.
    if (args && *args)
    {
        std::string name;
        Player* bot = ExtractPartyBotTarget(&args, name);
        if (!bot)
            return false;
        if (Order(bot, stay))
            BotShared::BotChat::SayToParty(bot, SayLine(player, bot, stay ? "talk.stay.ok" : "talk.follow.ok"));
        PSendSysMessage("PBOK;%s;%s;1", name.c_str(), what);
        return true;
    }

    // Everyone of the group, wherever they are.
    Group* group = player->GetGroup();
    if (!group)
    {
        SendSysMessage("PBERR;;notgroup");
        SetSentErrorMessage(true);
        return false;
    }
    uint32 changed = 0;
    Player* speaker = nullptr;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->getSource();
        if (!member || member == player || !Order(member, stay))
            continue;
        ++changed;
        if (!speaker || urand(0, changed - 1) == 0)
            speaker = member;
    }
    if (speaker)
        BotShared::BotChat::SayToParty(speaker, SayLine(player, speaker, stay ? "talk.stay.team" : "talk.follow.team"));
    PSendSysMessage("PBOK;;%s;%u", what, changed);
    return true;
}

bool ChatHandler::HandlePartyBotStayCommand(char* args)
{
    return HandlePartyBotStayHelper(args, true);
}

bool ChatHandler::HandlePartyBotFollowCommand(char* args)
{
    return HandlePartyBotStayHelper(args, false);
}
