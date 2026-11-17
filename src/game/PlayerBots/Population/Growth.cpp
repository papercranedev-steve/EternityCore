#include "Population/Growth.h"
#include "Population/Roster.h"
#include "Player.h"     // EQUIPMENT_SLOT_*
#include <algorithm>
#include <cmath>
#include <random>

namespace Population
{
    namespace Growth
    {
        uint8 Level(RosterMember const& member, uint8 anchorLevel, uint8 leadCap)
        {
            int32 const cap = std::min<int32>(int32(std::max<uint8>(anchorLevel, 1)) + leadCap, 60);

            int32 level;
            if (member.stallLevel)
                level = member.stallLevel;
            else
                level = int32(std::lround(float(anchorLevel) * member.pace)) + member.jitter;

            return uint8(std::min(std::max(level, 1), cap));
        }

        bool IsRetired(RosterMember const& member, uint8 anchorLevel)
        {
            return member.quitLevel && anchorLevel > member.quitLevel;
        }

        namespace
        {
            uint32 Seed(RosterMember const& member, uint8 level, uint32 salt)
            {
                uint32 const s = member.id * 2654435761u ^ (uint32(level) * 40503u) ^ salt;
                return s ? s : 1;
            }

            uint32 Positive(int32 value)
            {
                return value > 0 ? uint32(value) : 0;
            }
        }

        GearPlan Gear(RosterMember const& member, uint8 level, uint32 gearCap)
        {
            int32 const w = member.wealth;     // 0-100, most are low
            GearPlan plan;
            plan.seed = Seed(member, level, 0x9e37u);

            enum { POOR, COMMON, UNCOMMON, RARE, EPIC };
            if (level < 60)
            {
                plan.maxItemLevel = level + 2 + (w >= 70 ? 2 : 0);
                plan.qualityWeights[POOR] = Positive(30 - w / 5);
                plan.qualityWeights[COMMON] = Positive(45 - w / 5);
                plan.qualityWeights[UNCOMMON] = Positive(20 + w / 3);
                plan.qualityWeights[RARE] = Positive(w / 15);
            }
            else
            {
                // Nobody above the owner's team; poorer members stay further below.
                int32 const cap = int32(std::max<uint32>(gearCap, 60));
                int32 const margin = (100 - w) * 8 / 100;
                plan.maxItemLevel = uint32(std::max(cap - margin, 56));
                plan.qualityWeights[COMMON] = 4;
                plan.qualityWeights[UNCOMMON] = Positive(55 - w / 4);
                plan.qualityWeights[RARE] = Positive(30 + w / 3);
                plan.qualityWeights[EPIC] = w > 50 ? Positive((w - 50) / 2) : 1;
            }

            // Low levels have no jewelry yet and often no cloak, bracers or shoulders.
            std::mt19937 rng(Seed(member, level, 0x51edu));
            auto chance = [&rng](int32 pct) { return pct > 0 && int32(rng() % 100) < pct; };

            int32 const jewelryPct = 90 - int32(level) * 3 / 2 - w / 4;
            for (uint32 slot : { EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
                                 EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 })
                if (chance(jewelryPct))
                    plan.skipSlotMask |= 1u << slot;

            int32 const clothPct = 50 - int32(level) - w / 4;
            for (uint32 slot : { EQUIPMENT_SLOT_BACK, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_SHOULDERS })
                if (chance(clothPct))
                    plan.skipSlotMask |= 1u << slot;

            return plan;
        }

        uint32 Money(RosterMember const& member, uint8 level)
        {
            // At 60: 5 gold for the poorest, about 30 for a typical member, 250 for the richest.
            double const goldAt60 = 5.0 + double(member.wealth) * double(member.wealth) / 40.0;
            double const share = std::pow(double(std::max<uint8>(level, 1)) / 60.0, 2.5);
            std::mt19937 rng(Seed(member, level, 0x77u));
            double const jitter = 0.7 + double(rng() % 61) / 100.0;   // 0.7 - 1.3
            return uint32(goldAt60 * share * jitter * 10000.0);
        }
    }
}
