/*
 * Shared bot facts (PlayerBots/Shared): where a piece of gear comes from, so bots only wear
 * what a normal levelling player would have on.
 */

#ifndef MANGOS_BOTSHARED_GEARSOURCES_H
#define MANGOS_BOTSHARED_GEARSOURCES_H

#include "Common.h"

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
    }
}

#endif
