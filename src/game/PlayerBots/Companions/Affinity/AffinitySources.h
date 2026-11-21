/*
 * Where a companion's affinity comes from, besides the quests the player helps with (class quests:
 * ClassQuests/ClassQuestFlow; Artisan trips: ArtisanTrip). Numbers decided with the user
 * 2026-11-21, design .kiro/docs/2026-11-21-companion-affinity.md §2 (workspace root):
 *   recruit    starts at min(level at recruit, START_CAP)
 *   level-up   PER_LEVEL for every level it gains while its party has a real player in it
 *   money      MONEY_GIFT when it asked for money (a town trip, "pay") and the player gave some,
 *              at most MAX_MONEY_GIFTS times in its life
 *   clears     a max-level dungeon segment first 2, later 1; a raid first 5, later 3; Onyxia
 *              first 3, later 2 (no limit on repeats, user's choice). Only when the last boss dies.
 * Each gain goes to the leader as PBAFF (the addon prints it).
 *
 * Map thread (the bot's AI), except StartAt.
 */

#ifndef MANGOS_COMPANIONS_AFFINITY_SOURCES_H
#define MANGOS_COMPANIONS_AFFINITY_SOURCES_H

#include "Common.h"
#include "Shared/DungeonClears.h"
#include <string>
#include <vector>

class Map;
class Player;

namespace Companions
{
    namespace AffinitySources
    {
        uint32 const START_CAP = 15;
        uint32 const PER_LEVEL = 1;
        uint32 const MONEY_GIFT = 1;
        uint32 const MAX_MONEY_GIFTS = 3;

        // The affinity a companion recruited at `level` starts with.
        uint32 StartAt(uint32 level);

        // Per bot (a member of its AI).
        struct State
        {
            uint32 lastLevel = 0;       // 0 = not looked yet
            // The affinity it last told the leader about (Gates opened since: one line each);
            // UINT32_MAX = not looked yet (no lines for what it had at login).
            uint32 lastAffinity = UINT32_MAX;
        };

        // Every bot tick (cheap). `paidCopper`: what the leader gave it this tick for a town trip
        // it asked money for (Life::TownTrip::TakePaid), 0 = nothing. `tags`: its personality.
        // Also says a line in party chat for every ready gate (Gates) its affinity reached
        // since the last tick, whatever the source.
        void Update(Player* me, Player* leader, State& state, uint32 paidCopper, std::vector<std::string> const& tags);

        // A max-level dungeon segment or raid cleared (its last boss died). Every companion in the
        // instance whose party has a real player there gets ClearPoints. DungeonClears listener,
        // the instance's map thread.
        struct ClearPoints
        {
            uint32 first;
            uint32 later;
        };
        ClearPoints PointsFor(BotShared::DungeonClears::Info const& segment);
        void OnClear(Map* map, BotShared::DungeonClears::Info const& segment);
    }
}

#endif
