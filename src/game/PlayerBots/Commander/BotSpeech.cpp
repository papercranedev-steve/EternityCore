#include "Commander/BotSpeech.h"
#include "Commander/CommanderMgr.h"
#include "Commander/CommanderTypes.h"
#include "Commander/PlayerChat.h"
#include "PartyBotAI.h"
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
        // Module id (Proposal::source) -> what the line starts with. moves: its walks and holds
        // are decisions too (a patrol dodge, waiting for the tank's threat), not walking every tick.
        struct SourceText
        {
            char const* source;
            char const* label;
            bool moves;
        };

        SourceText const SOURCES[] = {
            { "interrupt",   "打断",   false },
            { "cc",          "控制",   true  },
            { "rescue",      "救人",   false },
            { "dispel",      "驱散",   false },
            { "fleestop",    "拦逃跑", false },
            { "aoe",         "范围",   true  },
            { "escape",      "脱身",   true  },
            { "threat",      "仇恨",   true  },
            { "pull",        "开怪",   true  },
            { "patrol",      "巡逻",   true  },
            { "position",    "走位",   true  },
            { "retreat",     "撤退",   true  },
            { "mana",        "回蓝",   false },
            { "target",      "换目标", false },
            { "encounter",   "首领",   true  },
        };

        SourceText const* FindSource(char const* source)
        {
            for (SourceText const& s : SOURCES)
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

        char const* MoveText(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::MoveTo:
                case BotActionType::Backpedal:  return "走位";
                case BotActionType::Chase:      return "追上去";
                case BotActionType::StopMove:
                case BotActionType::StopChase:  return "停下";
                case BotActionType::HoldFire:   return "停手";
                case BotActionType::Retreat:    return "撤退";
                case BotActionType::UseObject:  return "使用物体";
                default:                        return nullptr;
            }
        }
    }

    void BotSpeech::OnExecuted(PartyBotAI const* bot, Proposal const& p, uint32 nowMs)
    {
        if (!sCommanderMgr.IsSpeechEnabled() || p.kind != ProposalKind::Replace || p.tier == ProposalTier::Routine)
            return;
        Player* me = bot->me;
        if (!me->GetGroup())
            return;

        SourceText const* src = FindSource(p.source);
        char const* label = src ? src->label : p.source;
        BotAction const& a = p.action;
        std::string target;
        if (!a.target.IsEmpty())
        {
            Unit* unit = me->GetMap()->GetUnit(a.target);
            target = unit ? unit->GetName() : "?";
        }

        std::string key = p.source;
        std::string text = std::string(label) + "：";
        if (a.type == BotActionType::Cast && a.spell)
        {
            key += ":spell" + std::to_string(a.spell->Id);
            text += a.spell->SpellName[0];
            text += " → " + (target.empty() ? std::string("自己") : target);
        }
        else if (a.type == BotActionType::Attack)
        {
            key += ":attack" + std::to_string(a.target.GetRawValue());
            text += "打 " + target;
        }
        else if (char const* move = MoveText(a.type))
        {
            if (!src || !src->moves)
                return;
            key += ":" + std::string(move);
            text += move;
            if (!target.empty())
                text += " " + target;
        }
        else
            return;     // Hold ("keep walking" / "wait" every tick), rest, pet actions

        if (p.reason && *p.reason)
            text += std::string("（") + p.reason + "）";

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
