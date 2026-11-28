#include "Shared/GearStages.h"
#include <cstring>
#include <string>

namespace BotShared
{
namespace GearStages
{
    namespace
    {
        uint32 const MOLTEN_CORE = 409, ONYXIA = 249, ZUL_GURUB = 309, BLACKWING_LAIR = 469,
                     RUINS_OF_AHNQIRAJ = 509, TEMPLE_OF_AHNQIRAJ = 531, NAXXRAMAS = 533;

        // quality: poor, common, uncommon, rare, epic
        // source:  world drop, quest, dungeon, raid, crafted, vendor, PvP
        std::vector<StageDef> const STAGES =
        {
            { Stage::Leveling, "leveling", -6, 2,
                { { 0, 15, 80,  5, 0 }, { 45, 45,  5, 0,  5, 5, 0 } },
                { { 0,  5, 75, 20, 0 }, { 35, 40, 20, 0,  5, 5, 0 } },
                20, 40, 80, 16, 50, 15, {} },
            { Stage::Dungeon, "dungeon", -5, 3,
                { { 0, 0, 55, 45, 0 }, { 25, 35, 35, 0,  5, 0, 0 } },
                { { 0, 0, 35, 63, 2 }, { 15, 30, 50, 0,  5, 0, 0 } },
                15, 35, 60, 16, 40, 15, {} },
            { Stage::Fresh60, "fresh60", -6, 3,
                { { 0, 0, 60, 40, 0 }, { 35, 40, 20, 0,  5, 0, 0 } },
                { { 0, 0, 45, 53, 2 }, { 25, 35, 35, 0,  5, 0, 0 } },
                0, 55, 0, 0, 0, 0, {} },
            { Stage::PreRaid, "preraid", -2, 8,
                { { 0, 0, 10, 80, 10 }, { 5, 20, 60, 0, 15, 0, 0 } },
                { { 0, 0,  5, 75, 20 }, { 5, 15, 60, 0, 20, 0, 0 } },
                0, 55, 0, 0, 0, 0, {} },
            { Stage::RaidT1, "raid1", 0, 18,
                { { 0, 0, 0, 55, 45 }, { 0, 10, 40, 50, 0, 0, 0 } },
                { { 0, 0, 0, 35, 65 }, { 0,  5, 30, 65, 0, 0, 0 } },
                0, 55, 0, 0, 0, 0, { MOLTEN_CORE, ONYXIA, ZUL_GURUB } },
            { Stage::RaidT2, "raid2", 6, 23,
                { { 0, 0, 0, 30, 70 }, { 0, 5, 15, 75, 5, 0, 0 } },
                { { 0, 0, 0, 15, 85 }, { 0, 5, 10, 80, 5, 0, 0 } },
                0, 55, 0, 0, 0, 0, { MOLTEN_CORE, ONYXIA, ZUL_GURUB, BLACKWING_LAIR, RUINS_OF_AHNQIRAJ } },
            { Stage::RaidT3, "raid3", 10, 32,
                { { 0, 0, 0, 15, 85 }, { 0, 10, 5, 80, 5, 0, 0 } },
                { { 0, 0, 0,  5, 95 }, { 0, 10, 0, 85, 5, 0, 0 } },
                0, 55, 0, 0, 0, 0, { MOLTEN_CORE, ONYXIA, ZUL_GURUB, BLACKWING_LAIR, RUINS_OF_AHNQIRAJ,
                                     TEMPLE_OF_AHNQIRAJ, NAXXRAMAS } },
        };
    }

    StageDef const* Find(char const* name)
    {
        for (StageDef const& def : STAGES)
            if (name && !strcmp(def.name, name))
                return &def;
        return nullptr;
    }

    StageDef const& Get(Stage stage)
    {
        for (StageDef const& def : STAGES)
            if (def.stage == stage)
                return def;
        return STAGES.front();
    }

    std::string Names()
    {
        std::string out;
        for (StageDef const& def : STAGES)
        {
            if (!out.empty())
                out += ", ";
            out += def.name;
        }
        return out;
    }
}
}
