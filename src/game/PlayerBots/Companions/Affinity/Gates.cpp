#include "Companions/Affinity/Gates.h"
#include "Companions/CompanionMgr.h"
#include "SharedDefines.h"

namespace Companions
{
    namespace Gates
    {
        namespace
        {
            uint32 constexpr WARLOCK = 1 << (CLASS_WARLOCK - 1);
            uint32 constexpr MAGE = 1 << (CLASS_MAGE - 1);

            // Order: by affinity (the addon lists them so).
            std::vector<Info> const g_gates = {
                { Gate::TradeBasics,   6,   0,       true,  "trade",    "交易给他金币、吃喝、更好的装备和背包" },
                { Gate::TradeAnything, 15,  0,       true,  "tradeany", "交易给他任何物品" },
                { Gate::ManageBags,    22,  0,       true,  "bags",     "管理他的背包" },
                { Gate::WarlockSummon, 25,  WARLOCK, true,  "summon",   "请他召唤你" },
                { Gate::Crafting,      30,  0,       true,  "craft",    "用你的材料请他代工" },
                { Gate::MagePortal,    45,  MAGE,    true,  "portal",   "请他开传送门" },
                { Gate::TakeMoney,     50,  0,       true,  "money",    "从他那里拿钱" },
                { Gate::PickRewards,   60,  0,       true,  "rewards",  "帮他挑任务奖励" },
                { Gate::JoinAccount,   100, 0,       false, "account",  "招募他进你的账号" },
            };
        }

        std::vector<Info> const& All()
        {
            return g_gates;
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
