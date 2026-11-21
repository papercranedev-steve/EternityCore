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
 *   .partybot recipes <bot> <skill>       the craft window: what he can make for you (Gates Crafting)
 *     PBRCP;<bot>;BEGIN;<skill>;<value>;<max>
 *     PBRCP;<bot>;R;<recipe spell>;<item>;<per cast>;<difficulty 0 orange .. 3 grey>;<category>;<item:count,...>
 *            (category: Shared/Crafting::CategoryOf, "potion" .. or "<item class>.<subclass>")
 *     PBRCP;<bot>;END
 *     PBERR;<bot>;noskill | locked;<needed>;<now>
 *   .partybot craft <bot> <recipe spell> <count, 0 = all>   he makes it with your materials
 *   .partybot craft <bot> stop                               (Life/Errands/CraftForPlayer)
 *     PBOK;<bot>;craft;<spell>;<count>, then PBCRAFT;<bot>;made;<item>;<made>;<left> per item and
 *     PBCRAFT;<bot>;stop;<done|nomats|far|combat|stopped|bagsfull|failed|cooldown|gone> at the end
 *     PBERR;<bot>;norecipe | far | notgroup
 *   .partybot summon [warlock]            a warlock of the team summons you (Companions/Travel/SummonOffer;
 *                                         no name: the first warlock party bot that can)
 *   .partybot summon all                  the warlocks summon the bots left behind now (GatherPlanner)
 *     PBOK;;gather (they say who they summon) | PBERR;;nowarlock (no team on your map yet)
 *     PBOK;<warlock>;summon | PBERR;<warlock>;<notlearned|locked|noshard|nohelpers|fighting|here|near|dead|busy>
 *     PBERR;;nowarlock
 *   .partybot stay [bot]                  he (no name: every party bot of your group) waits here
 *   .partybot follow [bot]                he (no name: everyone waiting) follows you again
 *     PBOK;<bot or empty>;stay|follow;<how many>   (one of them says so in party chat)
 */

#include "Chat.h"
#include "Player.h"
#include "Group.h"
#include "Companions/Talk/Talk.h"
#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Affinity/Gates.h"
#include "Companions/Travel/SummonOffer.h"
#include "Population/StrangerTalk.h"
#include "ObjectMgr.h"
#include "Commander/CommanderMgr.h"
#include "Life/Errands/CraftForPlayer.h"
#include "Shared/Phrases.h"
#include "Shared/BotChat.h"
#include "Shared/Crafting.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <cstring>
#include "PartyBotAI.h"
#include "WorldSession.h"
#include "Utilities/Random.h"
#include <sstream>

namespace
{
    void SendPage(ChatHandler& handler, Player* bot, Companions::Talk::Page const& page)
    {
        Companions::Talk::SendPage(handler.GetSession() ? handler.GetSession()->GetPlayer() : nullptr, bot, page);
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
    // One of the people of the world (not of the party): he talks as a stranger
    // (Population/StrangerTalk, recruiting doc §2.1).
    if (botArg && *botArg)
    {
        std::string strangerName = botArg;
        Player* stranger = normalizePlayerName(strangerName) ? sObjectMgr.GetPlayer(strangerName.c_str()) : nullptr;
        if (stranger && stranger->IsInWorld() && Population::StrangerTalk::Is(stranger))
            return Population::StrangerTalk::Handle(*this, player, stranger, ExtractArg(&args));
    }
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

bool ChatHandler::HandlePartyBotRecipesCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    std::string name;
    char* botArg = ExtractArg(&args);
    Player* bot = player ? ExtractPartyBotTarget(&botArg, name) : nullptr;
    if (!bot || !PartyBotGateAllows(bot, name, uint8(Companions::Gates::Gate::Crafting)))
        return false;
    uint32 skill = 0;
    if (!ExtractUInt32(&args, skill) || !Companions::Talk::CraftsFor(bot, skill))
    {
        PSendSysMessage("PBERR;%s;noskill", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    namespace Crafting = BotShared::Crafting;
    uint32 const value = bot->GetSkillValuePure(uint16(skill));
    PSendSysMessage("PBRCP;%s;BEGIN;%u;%u;%u", name.c_str(), skill, value, uint32(bot->GetSkillMaxPure(uint16(skill))));
    for (SpellEntry const* recipe : Crafting::KnownRecipesOf(bot, skill))
    {
        Crafting::Product const product = Crafting::ProductOf(recipe);
        std::ostringstream line;
        line << "PBRCP;" << name << ";R;" << recipe->Id << ";" << product.itemId << ";" << product.perCast << ";"
             << uint32(Crafting::Difficulty(recipe, value)) << ";" << Crafting::CategoryOf(recipe) << ";";
        bool first = true;
        for (Crafting::Reagent const& r : Crafting::ReagentsOf(recipe))
        {
            line << (first ? "" : ",") << r.itemId << ":" << r.count;
            first = false;
        }
        SendSysMessage(line.str().c_str());
    }
    PSendSysMessage("PBRCP;%s;END", name.c_str());
    return true;
}

bool ChatHandler::HandlePartyBotCraftCommand(char* args)
{
    namespace Talk = Companions::Talk;
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    std::string name;
    char* botArg = ExtractArg(&args);
    Player* bot = player ? ExtractPartyBotTarget(&botArg, name) : nullptr;
    if (!bot)
        return false;
    PartyBotAI* ai = dynamic_cast<PartyBotAI*>(bot->AI());

    if (args && strncmp(args, "stop", 4) == 0)
    {
        Life::CraftForPlayer::Stop(bot, ai->m_craftForPlayer, Life::CraftForPlayer::Reason::Stopped);
        PSendSysMessage("PBOK;%s;craft;stop", name.c_str());
        return true;
    }

    if (!PartyBotGateAllows(bot, name, uint8(Companions::Gates::Gate::Crafting)))
        return false;
    uint32 spellId = 0, count = 0;
    if (!ExtractUInt32(&args, spellId) || !ExtractUInt32(&args, count))
    {
        PSendSysMessage("PBERR;%s;usage;.partybot craft <bot> <recipe spell> <count, 0 = all> | stop", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    std::string busyLine;
    switch (Talk::Check(player, bot, busyLine))
    {
        case Talk::Refusal::None:
            break;
        case Talk::Refusal::Busy:
            BotShared::BotChat::Say(bot, busyLine);
            PSendSysMessage("PBCRAFT;%s;stop;combat", name.c_str());
            return true;
        case Talk::Refusal::Far:
            PSendSysMessage("PBERR;%s;far", name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage("PBERR;%s;notgroup", name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    SpellEntry const* recipe = sSpellMgr.GetSpellEntry(spellId);
    if (!recipe || !bot->HasSpell(spellId) || !Talk::CraftsFor(bot, BotShared::Crafting::SkillOf(recipe)))
    {
        PSendSysMessage("PBERR;%s;norecipe", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    if (!Life::CraftForPlayer::Start(bot, ai->m_craftForPlayer, player, spellId, count, ai->PersonalityTags()))
    {
        PSendSysMessage("PBCRAFT;%s;stop;nomats", name.c_str());
        return true;
    }
    BotShared::BotChat::Say(bot, SayLine(player, bot, "craft.start"));
    PSendSysMessage("PBOK;%s;craft;%u;%u", name.c_str(), spellId, count);
    return true;
}

bool ChatHandler::HandlePartyBotSummonCommand(char* args)
{
    namespace Offer = Companions::SummonOffer;
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!player)
        return false;

    // all: the team's warlocks summon everyone left behind now (Commander GatherPlanner)
    if (args && strncmp(args, "all", 3) == 0)
    {
        if (!sCommanderMgr.GatherNow(player))
        {
            SendSysMessage("PBERR;;nowarlock");
            SetSentErrorMessage(true);
            return false;
        }
        SendSysMessage("PBOK;;gather");
        return true;
    }

    std::vector<Player*> warlocks;
    std::string name;
    if (args && *args)
    {
        Player* bot = ExtractPartyBotTarget(&args, name);
        if (!bot)
            return false;
        warlocks.push_back(bot);
    }
    else if (Group* group = player->GetGroup())
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* member = itr->getSource())
                if (member != player && member->GetClass() == CLASS_WARLOCK && member->IsInWorld() &&
                    member->AI() && dynamic_cast<PartyBotAI*>(member->AI()))
                    warlocks.push_back(member);
    if (warlocks.empty())
    {
        SendSysMessage("PBERR;;nowarlock");
        SetSentErrorMessage(true);
        return false;
    }

    // The first that can (no name: any warlock of the group); else the first one's reason.
    Player* chosen = warlocks.front();
    for (Player* w : warlocks)
    {
        std::vector<ObjectGuid> helpers;
        if (Offer::Check(w, player, helpers) == Offer::Refusal::None)
        {
            chosen = w;
            break;
        }
    }
    PartyBotAI* ai = dynamic_cast<PartyBotAI*>(chosen->AI());
    Offer::Refusal const refusal = Offer::Begin(chosen, player, ai ? ai->PersonalityTags() : std::vector<std::string>());
    if (refusal != Offer::Refusal::None)
    {
        PSendSysMessage("PBERR;%s;%s", chosen->GetName(), Offer::KeyOf(refusal));
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;%s;summon", chosen->GetName());
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
