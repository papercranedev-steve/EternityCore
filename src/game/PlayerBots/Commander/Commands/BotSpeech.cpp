#include "Commander/Commands/BotSpeech.h"
#include "Commander/CommanderMgr.h"
#include "Commander/CommanderTypes.h"
#include "Commander/Commands/PlayerChat.h"
#include "Shared/Phrases.h"
#include "TeamBotAI.h"
#include "Chat.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

namespace Commander
{
    namespace
    {
        // Module id (Proposal::source): what the line starts with is i18n "speech.source.<id>".
        // moves: its walks and holds are decisions too (a patrol dodge, waiting for the tank's
        // threat), not walking every tick.
        struct SourceInfo
        {
            char const* source;
            bool moves;
        };

        SourceInfo const SOURCES[] = {
            { "interrupt", false }, { "cc", true }, { "rescue", false }, { "dispel", false },
            { "fleestop", false }, { "aoe", true }, { "escape", true }, { "threat", true },
            { "pull", true }, { "lure", true }, { "position", true }, { "retreat", true },
            { "mana", false }, { "target", false }, { "encounter", true },
        };

        SourceInfo const* FindSource(char const* source)
        {
            for (SourceInfo const& s : SOURCES)
                if (!strcmp(s.source, source))
                    return &s;
            return nullptr;
        }
        struct BotState
        {
            uint32 lastMs = 0;
            std::map<std::string, uint32> said;     // key -> when
        };

        std::mutex s_lock;
        std::unordered_map<uint64, BotState> s_bots;

        // True (and remembered) when the bot may say `key` now.
        bool Allow(ObjectGuid bot, std::string const& key, uint32 nowMs)
        {
            std::lock_guard<std::mutex> lock(s_lock);
            BotState& s = s_bots[bot.GetRawValue()];
            if (s.lastMs && WorldTimer::getMSTimeDiff(s.lastMs, nowMs) < BotSpeech::MIN_GAP_MS)
                return false;
            auto const itr = s.said.find(key);
            if (itr != s.said.end() && WorldTimer::getMSTimeDiff(itr->second, nowMs) < BotSpeech::REPEAT_MS)
                return false;
            if (s.said.size() > 64)
            {
                for (auto it = s.said.begin(); it != s.said.end();)
                    it = WorldTimer::getMSTimeDiff(it->second, nowMs) >= BotSpeech::REPEAT_MS ? s.said.erase(it) : std::next(it);
            }
            s.said[key] = nowMs;
            s.lastMs = nowMs;
            return true;
        }

        // i18n "speech.act.<kind>" of a move, null: not one.
        char const* MoveKind(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::MoveTo:
                case BotActionType::Backpedal:  return "move";
                case BotActionType::Chase:      return "chase";
                case BotActionType::StopMove:
                case BotActionType::StopChase:  return "stop";
                case BotActionType::HoldFire:   return "holdfire";
                case BotActionType::Retreat:    return "retreat";
                case BotActionType::UseObject:  return "useobject";
                default:                        return nullptr;
            }
        }
    }
    void BotSpeech::OnExecuted(TeamBotAI const* bot, Proposal const& p, uint32 nowMs)
    {
        if (!sCommanderMgr.IsSpeechEnabled() || p.kind != ProposalKind::Replace || p.tier == ProposalTier::Routine)
            return;
        Player* me = bot->me;
        if (!me->GetGroup())
            return;

        namespace Phrases = BotShared::Phrases;
        Phrases::Lang const lang = BotShared::I18n::OfParty(me);
        SourceInfo const* src = FindSource(p.source);
        std::string const label = src ? Phrases::Word(lang, std::string("speech.source.") + src->source) : std::string(p.source);
        BotAction const& a = p.action;
        std::string target;
        if (!a.target.IsEmpty())
        {
            Unit* unit = me->GetMap()->GetUnit(a.target);
            target = unit ? unit->GetName() : "?";
        }

        std::string key = p.source;
        std::string action;
        if (a.type == BotActionType::Cast && a.spell)
        {
            key += ":spell" + std::to_string(a.spell->Id);
            action = Phrases::Text(lang, "speech.cast", { { "spell", a.spell->SpellName[0] },
                { "target", target.empty() ? Phrases::Word(lang, "speech.self") : target } });
        }
        else if (a.type == BotActionType::Attack)
        {
            key += ":attack" + std::to_string(a.target.GetRawValue());
            action = Phrases::Text(lang, "speech.attack", { { "target", target } });
        }
        else if (char const* kind = MoveKind(a.type))
        {
            if (!src || !src->moves)
                return;
            key += ":" + std::string(kind);
            std::string const move = Phrases::Word(lang, std::string("speech.act.") + kind);
            action = target.empty() ? move : Phrases::Text(lang, "speech.move.target", { { "move", move }, { "target", target } });
        }
        else
            return;     // Hold ("keep walking" / "wait" every tick), rest, pet actions

        std::string text = Phrases::Text(lang, "speech.line", { { "label", label }, { "action", action } });
        if (p.reason && *p.reason)
            text = Phrases::Text(lang, "speech.reason", { { "line", text }, { "reason", p.reason } });

        if (Allow(me->GetObjectGuid(), key, nowMs))
            PlayerChat::SayToParty(me, text);
    }

    void BotSpeech::Report(Player* speaker, Player* leader, std::string const& text)
    {
        if (Say(speaker, text))
            return;
        if (leader && leader->IsInWorld() && (!speaker || leader->GetMap() == speaker->GetMap()))
            ChatHandler(leader).PSendSysMessage("[PartyBot] %s", text.c_str());
    }

    bool BotSpeech::Say(Player* speaker, std::string const& text)
    {
        if (!sCommanderMgr.IsSpeechEnabled() || !speaker)
            return false;
        return PlayerChat::SayToParty(speaker, text);
    }
}
