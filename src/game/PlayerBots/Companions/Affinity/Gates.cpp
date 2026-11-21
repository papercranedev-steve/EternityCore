#include "Companions/Affinity/Gates.h"
#include "Companions/CompanionMgr.h"
#include "Shared/Phrases.h"
#include "SharedDefines.h"

namespace Companions
{
    namespace Gates
    {
        namespace
        {
            uint32 constexpr WARLOCK = 1 << (CLASS_WARLOCK - 1);
            uint32 constexpr MAGE = 1 << (CLASS_MAGE - 1);

            // Order: by affinity (the addon lists them so). Labels (i18n "gate.<key>") are what the player reads on the
            // Companions panel's affinity ruler: keep them short and general (user 2026-11-21: the
            // first trade gate stays "和他做简单的交易", not a list of what he accepts).
            std::vector<Info> const g_gates = {
                { Gate::TradeBasics,   6,   0,       true,  "trade" },
                { Gate::TradeAnything, 15,  0,       true,  "tradeany" },
                { Gate::ManageBags,    22,  0,       true,  "bags" },
                { Gate::WarlockSummon, 25,  WARLOCK, true,  "summon" },
                { Gate::Crafting,      30,  0,       true,  "craft" },
                { Gate::MagePortal,    45,  MAGE,    true,  "portal" },
                { Gate::TakeMoney,     50,  0,       true,  "money" },
                { Gate::PickRewards,   60,  0,       true,  "rewards" },
                { Gate::JoinAccount,   100, 0,       true,  "account" },
            };
        }

        std::vector<Info> const& All()
        {
            return g_gates;
        }

        std::string Label(Info const& info, BotShared::I18n::Lang lang)
        {
            return BotShared::Phrases::Word(lang, std::string("gate.") + info.key);
        }

        Info const& Of(Gate gate)
        {
            for (Info const& info : g_gates)
                if (info.gate == gate)
                    return info;
            return g_gates.front();     // (every gate has a row)
        }

        bool Allows(uint32 charGuid, Gate gate)
        {
            if (!sCompanionMgr.IsCompanion(charGuid))
                return true;
            return sCompanionMgr.GetAffinity(charGuid) >= Of(gate).affinity;
        }

        std::vector<Info const*> Opened(uint32 before, uint32 after, uint8 classId)
        {
            std::vector<Info const*> opened;
            uint32 const mask = classId ? 1u << (classId - 1) : 0;
            for (Info const& info : g_gates)
                if (info.ready && info.affinity > before && info.affinity <= after &&
                    (!info.classMask || (info.classMask & mask)))
                    opened.push_back(&info);
            return opened;
        }
    }
}
