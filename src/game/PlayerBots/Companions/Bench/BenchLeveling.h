/*
 * Companions on the bench (not in the player's party, out of the world) keep up with him:
 * SHARE of the experience he really gains, up to his level minus LEVELS_BEHIND (user
 * 2026-11-21). No affinity for it (affinity counts level-ups in the party only).
 * Design: .kiro/docs/2026-11-21-companion-bench.md §2 (workspace root).
 *
 * Pure arithmetic; CompanionMgr::UpdateBench measures the player and writes the companions.
 */

#ifndef MANGOS_COMPANIONS_BENCH_LEVELING_H
#define MANGOS_COMPANIONS_BENCH_LEVELING_H

#include "Common.h"

class Player;

namespace Companions
{
    namespace BenchLeveling
    {
        // Share of the player's experience, in percent.
        uint32 const SHARE_PERCENT = 40;
        // A benched companion grows up to the player's level minus this.
        uint32 const LEVELS_BEHIND = 2;

        // Experience from level 1 to where `player` is now.
        uint32 TotalXp(Player const* player);

        // The companion's part of `ownerGained`.
        inline uint32 ShareOf(uint32 ownerGained) { return uint32(uint64(ownerGained) * SHARE_PERCENT / 100); }

        // The highest level a benched companion reaches next to an owner of `ownerLevel`.
        uint32 CapFor(uint32 ownerLevel);

        struct Progress
        {
            uint32 level = 1;
            uint32 xp = 0;      // into `level`
        };
        // `from` plus `gain` experience, not past `cap` (reaching it: its experience is 0 there).
        // Already at or above the cap: unchanged.
        Progress Grow(Progress from, uint32 gain, uint32 cap);
    }
}

#endif
