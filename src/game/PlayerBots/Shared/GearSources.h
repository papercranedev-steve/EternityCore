/*
 * Shared bot facts (PlayerBots/Shared): where a piece of gear comes from, so bots only wear
 * what a normal levelling player would have on.
 *
 * Sources (SourcesOf) are read from the world database once, on first use (Warm at startup):
 * creature and chest loot by the map they are spawned on (references followed), quest rewards,
 * vendors, the crafting spells and honor ranks. Random gear stages pick by them
 * (Shared/RandomGear, design .kiro/docs/2026-11-28-random-gear-profiles.md).
 *
 * World thread only for the first use (database), then read only.
 */

#ifndef MANGOS_BOTSHARED_GEARSOURCES_H
#define MANGOS_BOTSHARED_GEARSOURCES_H

#include "Common.h"
#include <vector>

struct ItemPrototype;

namespace BotShared
{
    namespace GearSources
    {
        // Not gear a player levels in: profession tools and gadgets (required skill, or a tool a
        // spell needs), fishing gear (fishing pole, +fishing items) and arena rewards.
        bool IsOddity(ItemPrototype const* proto);

        // Made by a spell (professions, conjuring).
        bool IsCrafted(uint32 itemId);

        enum Source : uint32
        {
            SOURCE_WORLD_DROP = 1 << 0,     // creatures and chests on the two continents
            SOURCE_QUEST      = 1 << 1,     // a quest reward
            SOURCE_DUNGEON    = 1 << 2,     // a five man dungeon's own loot (not its world drop pools)
            SOURCE_RAID       = 1 << 3,     // a raid's own loot
            SOURCE_CRAFTED    = 1 << 4,
            SOURCE_VENDOR     = 1 << 5,
            SOURCE_PVP        = 1 << 6,     // honor rank gear, battleground loot
        };
        uint32 const SOURCE_COUNT = 7;

        // Source bits of the item; 0 = no known way to get it (not worn by random gear stages).
        uint32 SourcesOf(ItemPrototype const* proto);

        // Dungeon and raid maps whose own loot has the item (empty: none).
        std::vector<uint32> const& InstanceMapsOf(uint32 itemId);

        // Builds the tables now (a second of database at startup instead of in a game).
        void Warm();
    }
}

#endif
