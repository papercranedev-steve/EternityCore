/*
 * Shared bot facts (PlayerBots/Shared): what one skill point of a crafting profession costs,
 * for skilling up without a real crafting session (a companion offline, Companions/OfflineLife).
 * Design: .kiro/docs/2026-11-21-companion-professions.md §4 (workspace root).
 *
 * At each skill value the cheapest trainer recipe per expected point: orange = 1 point a cast,
 * yellow (SkillLineAbility min_value .. max_value) = the share of the way to gray left. A cast
 * costs its reagents at a vendor's price (item_template buy_price, which cloth, herbs and dust
 * have too, about 4x their sell price) less what its product sells for. Each value costs at
 * least value^2 / 20 copper (products that sell above their reagents: smithing, alchemy), and a
 * value no trainer recipe levels takes the value below's price (players use other recipes).
 * Built once per skill.
 */

#ifndef MANGOS_BOTSHARED_SKILL_UP_COST_H
#define MANGOS_BOTSHARED_SKILL_UP_COST_H

#include "Common.h"

namespace BotShared
{
    namespace SkillUpCost
    {
        // Copper from `value` to value + 1 of a crafting skill (Shared/Professions), 0 = not a
        // crafting skill or value out of 1..299.
        uint32 OfPoint(uint32 skill, uint32 value);
    }
}

#endif
