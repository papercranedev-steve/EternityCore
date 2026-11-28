/*
 * Shared bot facts (PlayerBots/Shared): gear stages, how a player at some point of his game is
 * dressed (design .kiro/docs/2026-11-28-random-gear-profiles.md, user 2026-11-28). Data only:
 * a new stage is one more row in GearStages.cpp; Shared/RandomGear does the picking.
 *
 * Per equipment slot: a quality is drawn by the quality weights, then a source (Shared/
 * GearSources) by the source weights, among what exists in the item level window. Weights
 * slide from `low` to `high` with the strength (0-100) a caller asks for.
 * The poor look of population bots that never joined anyone is not a stage: it stays
 * RandomGearOptions (Population/Growth).
 *
 * Any thread (static data).
 */

#ifndef MANGOS_BOTSHARED_GEAR_STAGES_H
#define MANGOS_BOTSHARED_GEAR_STAGES_H

#include "Common.h"
#include "Shared/GearSources.h"
#include <string>
#include <vector>

namespace BotShared
{
    namespace GearStages
    {
        enum class Stage : uint8
        {
            Leveling,   // a normal levelling player: greens, a few blues (player teams, boss tests)
            Dungeon,    // has run the dungeons of his level: about half blue
            Fresh60,    // just reached 60: greens, quest blues, some dungeon blues
            PreRaid,    // done with the level 60 dungeons: blues, crafted and dungeon epics
            RaidT1,     // Molten Core, Onyxia, Zul'Gurub
            RaidT2,     // + Blackwing Lair, Ruins of Ahn'Qiraj
            RaidT3,     // + Temple of Ahn'Qiraj, Naxxramas
        };

        uint32 const QUALITIES = 5;     // poor, common, uncommon, rare, epic

        struct Mix
        {
            uint32 quality[QUALITIES];
            // GearSources::Source bit order: world drop, quest, dungeon, raid, crafted, vendor, PvP
            uint32 source[GearSources::SOURCE_COUNT];
        };

        struct StageDef
        {
            Stage stage;
            char const* name;               // Lua / commands
            int32 itemLevelBelow;           // window: level + below .. level + above
            int32 itemLevelAbove;
            Mix low;                        // at strength 0
            Mix high;                       // at strength 100
            uint32 commonUntilLevel;        // white items only below this level
            uint32 minTrinketLevel;         // trinket slots empty below
            // Empty slot chance in percent: base - perLevel * level (neck, rings, trinkets; and
            // cloak, wrists, shoulders), as low level players have none yet.
            int32 jewelryEmptyBase;
            int32 jewelryEmptyPerLevel10;   // per 10 levels
            int32 minorEmptyBase;
            int32 minorEmptyPerLevel10;
            std::vector<uint32> raidMaps;   // raid loot from these maps only
        };

        // nullptr: no such name.
        StageDef const* Find(char const* name);
        StageDef const& Get(Stage stage);
        // "leveling, dungeon, ..." for error messages.
        std::string Names();
    }
}

#endif
