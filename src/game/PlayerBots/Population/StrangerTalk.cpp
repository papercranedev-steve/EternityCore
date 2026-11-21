#include "Population/StrangerTalk.h"
#include "Population/PopulationBotAI.h"
#include "Companions/Talk/Talk.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "Shared/Capitals.h"
#include "Chat.h"
#include "Group.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <string>

namespace Population
{
namespace StrangerTalk
{
    namespace
    {
        namespace Talk = Companions::Talk;

        // Asked to join (recruiting doc §2.1): most say yes.
        int32 constexpr JOIN_BASE_PCT = 75;

        PopulationBotAI* AiOf(Player* bot)
        {
            return bot && bot->AI() ? dynamic_cast<PopulationBotAI*>(bot->AI()) : nullptr;
        }

        std::string Say(PopulationBotAI* ai, Player* player, std::string const& key, Life::Phrases::Vars vars = {})
        {
            vars["leader"] = player ? player->GetName() : "";
            return Life::Phrases::Say(key, ai->Tags(), vars);
        }

        Talk::Option OptionOf(char const* id, char const* wordKey, char const* fallback)
        {
            Talk::Option option;
            option.id = id;
            option.icon = Talk::Icon::Chat;
            option.text = Life::Phrases::Word(wordKey, fallback);
            return option;
        }

        std::vector<Talk::Option> MeetOptions(PopulationBotAI* ai, bool chat)
        {
            std::vector<Talk::Option> options;
            if (ai->CanJoin())
                options.push_back(OptionOf("meet:join", "meet.opt.join", "一起打一会儿怎么样？"));
            if (chat)
                options.push_back(OptionOf("meet:chat", "meet.opt.chat", "你在忙什么呢？"));
            return options;
        }

        std::string ClassWord(Player* bot)
        {
            return Life::Phrases::Word("class." + std::to_string(uint32(bot->GetClass())), "冒险者");
        }

        bool Heals(uint8 classId)
        {
            return classId == CLASS_PRIEST || classId == CLASS_PALADIN || classId == CLASS_DRUID || classId == CLASS_SHAMAN;
        }

        bool HasTag(PopulationBotAI* ai, char const* tag)
        {
            std::vector<std::string> const& tags = ai->Tags();
            return std::find(tags.begin(), tags.end(), tag) != tags.end();
        }

        int32 JoinChancePct(PopulationBotAI* ai, Player* bot)
        {
            int32 pct = JOIN_BASE_PCT;
            if (HasTag(ai, "chatty"))
                pct += 15;
            if (HasTag(ai, "quiet"))
                pct -= 15;
            if (HasTag(ai, "lazy"))
                pct -= 10;
            if (HasTag(ai, "homebody") && BotShared::Capitals::OfZone(bot->GetZoneId()))
                pct -= 25;
            return std::max<int32>(5, std::min<int32>(95, pct));
        }

        // His answer to "一起打一会儿？": the line he says; `joins` when he comes along.
        std::string DecideJoin(PopulationBotAI* ai, Player* player, Player* bot, bool& joins)
        {
            joins = false;
            if (!ai->CanJoin())
                return Say(ai, player, "meet.join.cannot");
            // (recruiting doc §1: never anyone above the player)
            if (bot->GetLevel() > player->GetLevel())
                return TooHighLine(bot, ai->Tags());
            if (player->GetGroup() && player->GetGroup()->IsFull())
                return Say(ai, player, "meet.join.full");

            PopulationBotAI::JoinAnswer& answer = ai->AnswerTo(player->GetGUIDLow());
            if (answer == PopulationBotAI::JoinAnswer::No)
                return Say(ai, player, "meet.join.again");
            if (answer == PopulationBotAI::JoinAnswer::Unasked)
                answer = int32(urand(0, 99)) < JoinChancePct(ai, bot) ? PopulationBotAI::JoinAnswer::Yes : PopulationBotAI::JoinAnswer::No;
            if (answer == PopulationBotAI::JoinAnswer::No)
                return Say(ai, player, "meet.join.no");
            joins = true;
            return Say(ai, player, "meet.join.yes");
        }

        char const* ChatKey(PopulationBotAI* ai)
        {
            switch (ai->GetRequest().kind)
            {
                case BotKind::City:  return "meet.chat.city";
                case BotKind::Field: return "meet.chat.field";
                default:             return "meet.chat.town";
            }
        }

        void FirstOfferPage(Player* player, Player* bot, PopulationBotAI* ai)
        {
            Life::Phrases::Vars vars;
            vars["name"] = bot->GetName();
            vars["class"] = ClassWord(bot);
            vars["help"] = Life::Phrases::Word(Heals(bot->GetClass()) ? "first.help.heal" : "first.help.tank",
                Heals(bot->GetClass()) ? "帮你治伤" : "帮你顶在前面");
            Talk::Page page;
            page.text = Say(ai, player, "first.offer", vars);
            page.options.push_back(OptionOf("first:yes", "first.opt.yes", "好啊，一起走吧。"));
            page.options.push_back(OptionOf("first:no", "first.opt.no", "我想先一个人走走。"));
            Talk::SendPage(player, bot, page);
        }
    }

    bool Is(Player* bot)
    {
        return AiOf(bot) != nullptr;
    }

    std::string TooHighLine(Player* bot, std::vector<std::string> const& tags)
    {
        Life::Phrases::Vars vars;
        vars["level"] = std::to_string(bot->GetLevel());
        return Life::Phrases::Say("meet.join.toohigh", tags, vars);
    }

    bool Handle(ChatHandler& handler, Player* player, Player* bot, char const* option)
    {
        PopulationBotAI* ai = AiOf(bot);
        std::string const name = bot->GetName();
        if (!ai || !player || !bot->IsInWorld() || !player->IsInMap(bot) || !bot->IsWithinDistInMap(player, MEET_YD))
        {
            handler.PSendSysMessage("PBERR;%s;far", name.c_str());
            return true;
        }
        if (!bot->IsAlive())
        {
            handler.PSendSysMessage("PBERR;%s;dead", name.c_str());
            return true;
        }
        std::string const opt = option ? option : "";

        // The herald's offer.
        if (ai->IsHeraldFor(player) && (opt == "first:yes" || opt == "first:no"))
        {
            if (opt == "first:yes")
            {
                HeraldAccepted(player, bot);
                ai->HeraldAnswer(player, true);
            }
            else
            {
                BotShared::BotChat::Say(bot, Say(ai, player, "first.no"));
                ai->HeraldAnswer(player, false);
            }
            handler.PSendSysMessage("PBTALK;%s;CLOSE", name.c_str());
            return true;
        }

        if (bot->IsInCombat() || player->IsInCombat())
        {
            std::string const line = Say(ai, player, "talk.busy");
            BotShared::BotChat::Say(bot, line);
            handler.PSendSysMessage("PBTALK;%s;BUSY;%s", name.c_str(), line.c_str());
            return true;
        }

        // (the herald talks on his own: the window shows his offer)
        if (ai->IsHeraldFor(player))
        {
            FirstOfferPage(player, bot, ai);
            return true;
        }

        ai->HoldForTalk(player);
        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);

        if (opt == "meet:join")
        {
            bool joins = false;
            std::string const line = DecideJoin(ai, player, bot, joins);
            BotShared::BotChat::Say(bot, line);
            if (joins)
                ai->Join(player, false);
            handler.PSendSysMessage("PBTALK;%s;CLOSE", name.c_str());
            return true;
        }

        Talk::Page page;
        if (opt == "meet:chat")
        {
            page.text = Say(ai, player, ChatKey(ai));
            page.options = MeetOptions(ai, false);
        }
        else
        {
            page.text = Say(ai, player, "meet.hello");
            page.options = MeetOptions(ai, true);
        }
        Talk::SendPage(player, bot, page);
        return true;
    }

    void OnInvite(Player* player, Player* bot)
    {
        PopulationBotAI* ai = AiOf(bot);
        if (!ai || !player || !player->IsInWorld())
            return;
        // Too far to talk: "do we know each other?"
        if (!player->IsInMap(bot) || !bot->IsWithinDistInMap(player, MEET_YD))
        {
            BotShared::BotChat::WhisperTo(bot, player, Say(ai, player, "meet.invite.far"));
            return;
        }
        ai->HoldForTalk(player);
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        Talk::Page page;
        page.text = Say(ai, player, "meet.invited");
        page.options = MeetOptions(ai, true);
        Talk::SendPage(player, bot, page);
    }

    bool HeraldSpeaks(Player* player, Player* bot, uint32 index)
    {
        PopulationBotAI* ai = AiOf(bot);
        if (!ai || !player)
            return false;
        switch (index)
        {
            case 0:
                bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
                BotShared::BotChat::Say(bot, Say(ai, player, "first.hello"));
                return true;
            case 1:
            {
                Life::Phrases::Vars vars;
                vars["giver"] = ai->GetRequest().giverName.empty() ? Life::Phrases::Word("first.giver", "营地的长官")
                                                                   : ai->GetRequest().giverName;
                bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                BotShared::BotChat::Say(bot, Say(ai, player, "first.why", vars));
                return true;
            }
            default:
                bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
                FirstOfferPage(player, bot, ai);
                return false;
        }
    }

    void HeraldAccepted(Player* player, Player* bot)
    {
        PopulationBotAI* ai = AiOf(bot);
        if (!ai)
            return;
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
        BotShared::BotChat::Say(bot, Say(ai, player, "first.yes"));
    }

    void HeraldGivesUp(Player* player, Player* bot)
    {
        PopulationBotAI* ai = AiOf(bot);
        if (!ai)
            return;
        BotShared::BotChat::Say(bot, Say(ai, player, "first.no"));
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).PSendSysMessage("PBTALK;%s;CLOSE", bot->GetName());
    }
}
}
