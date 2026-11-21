/*
 * Companion slots: how many companions a player may have, in his party and on the bench
 * together (former companions, "故人", take none). Free ones open with his level, more are
 * bought for gold once all free ones are open, up to a maximum (user 2026-11-21).
 * Design: .kiro/docs/2026-11-21-companion-slots.md (workspace root).
 *
 * Rules only, from the config (Companion.Slots.*); who has how many is CompanionMgr's.
 */

#ifndef MANGOS_COMPANIONS_SLOTS_H
#define MANGOS_COMPANIONS_SLOTS_H

#include "Common.h"

namespace Companions
{
    namespace Slots
    {
        // Companion.Slots.FreeLevels, .Prices, .Max (called from CompanionMgr::LoadConfig).
        void LoadConfig();

        // Free slots a player of `level` has.
        uint32 FreeAt(uint32 level);
        // The level his next free slot opens at; 0: all are open.
        uint32 NextFreeLevel(uint32 level);
        // How many slots there can be at most (free and bought).
        uint32 Max();
        // Copper the slot number `slot` (1-based: the 9th slot is 9) costs to buy: the config's
        // gold (at money rate 1) times Rate.Drop.Money. 0 for a free one or one past Max
        // (whether it can be bought is the caller's: all free ones open).
        uint32 PriceOf(uint32 slot);
    }
}

#endif
