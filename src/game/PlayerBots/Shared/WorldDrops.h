/*
 * Shared bot facts (PlayerBots/Shared): the open world's random drops ("world drops"), by the
 * world database. A loot reference (reference_loot_template) is a world drop pool when
 * creatures spawned on the two continents (maps 0 and 1) use it, at least MIN_CREATURES of
 * them: dungeon and boss loot is never in such a pool. Its items of the server's patch that
 * can be traded (no bind on pickup, not a quest item).
 * For a benched companion's surprise (Companions/Bench/Expedition; bench doc §5.3, workspace
 * root .kiro/docs).
 *
 * World thread only (built on first use).
 */

#ifndef MANGOS_BOTSHARED_WORLD_DROPS_H
#define MANGOS_BOTSHARED_WORLD_DROPS_H

#include "Common.h"
#include <vector>

namespace BotShared
{
    namespace WorldDrops
    {
        uint32 const MIN_CREATURES = 10;
        // An item for a player of level L: it asks for L - LEVELS_BELOW .. L (item level - 5
        // when it asks for none).
        uint32 const LEVELS_BELOW = 4;

        // World drops of `quality` (ITEM_QUALITY_*) for a player of `level`; empty: none.
        std::vector<uint32> For(uint32 quality, uint32 level);
    }
}

#endif
