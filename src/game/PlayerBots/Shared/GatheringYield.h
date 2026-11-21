/*
 * Shared bot facts (PlayerBots/Shared): what gathering brings in at a skill value, by the world
 * database, for gathering without going out (a benched companion's expedition parcel,
 * Companions/Bench/Expedition; bench doc §5.1, workspace root .kiro/docs).
 *
 * Herbalism, mining: chest templates (gameobject_template type 3) whose lock (Lock.dbc, data0)
 * asks for the skill; their loot (gameobject_loot_template, data1). Skinning: creatures with
 * skinning loot (skinning_loot_template) of the highest level the skill can skin
 * (Gathering::SkinningReq). Only the current tier: nodes asked for within TIER of the value
 * (the highest below it when none is), corpses within SKIN_LEVELS of that level. Plain loot
 * rows of the server's patch (no references, no quest chances).
 *
 * World thread only (built on first use, cached).
 */

#ifndef MANGOS_BOTSHARED_GATHERING_YIELD_H
#define MANGOS_BOTSHARED_GATHERING_YIELD_H

#include "Common.h"
#include <vector>

namespace BotShared
{
    namespace GatheringYield
    {
        uint32 const TIER = 50;
        uint32 const SKIN_LEVELS = 5;

        struct Yield
        {
            uint32 itemId = 0;
            float weight = 0.0f;    // items per node or corpse, summed over the tier's ones
        };

        // Empty: not a gathering skill, or nothing found.
        std::vector<Yield> const& At(uint32 skill, uint32 value);
    }
}

#endif
