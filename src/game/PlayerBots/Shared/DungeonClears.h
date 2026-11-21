/*
 * Which max-level dungeon or raid was just cleared: a segment (a dungeon wing, a raid) counts as
 * cleared when its last boss dies. The game itself has no "cleared" event (instance scripts keep
 * their own boss states), so this is a list of last bosses and one call from Unit::Kill.
 * Used for companion affinity (Companions/Affinity/AffinitySources; design
 * .kiro/docs/2026-11-21-companion-affinity.md §4, workspace root). Blackrock Depths does not count.
 *
 * Kill hook: the map thread of the instance. The listener runs there too.
 */

#ifndef MANGOS_BOTSHARED_DUNGEON_CLEARS_H
#define MANGOS_BOTSHARED_DUNGEON_CLEARS_H

#include "Common.h"
#include <vector>

class Creature;
class Map;

namespace BotShared
{
    namespace DungeonClears
    {
        // Stored in the database (custom_companion_clear.segment): never renumber.
        enum class Segment : uint8
        {
            None = 0,
            StratholmeLive = 1,
            StratholmeUndead = 2,
            Scholomance = 3,
            LowerBlackrock = 4,
            UpperBlackrock = 5,
            DireMaulEast = 6,
            DireMaulWest = 7,
            DireMaulNorth = 8,
            MoltenCore = 20,
            Onyxia = 21,
            ZulGurub = 22,
            BlackwingLair = 23,
            RuinsOfAhnQiraj = 24,
            TempleOfAhnQiraj = 25,
            Naxxramas = 26,
        };

        struct Info
        {
            Segment segment;
            uint32 mapId;
            uint32 lastBoss;    // creature entry
            bool raid;
            char const* name;
        };

        std::vector<Info> const& All();
        // The segment whose last boss this is, nullptr = none.
        Info const* OfLastBoss(uint32 mapId, uint32 creatureEntry);

        // Told about every clear (one listener; set at startup).
        using Listener = void (*)(Map* map, Info const& segment);
        void SetListener(Listener listener);

        // Any dungeon or raid, levelling ones too: its (or one wing's) last boss. Used by
        // companion recruiting (a trial partner who cleared a dungeon with the player, design
        // .kiro/docs/2026-11-21-companion-recruiting.md §2.3). Blackrock Depths counts here.
        bool IsDungeonEnd(uint32 mapId, uint32 creatureEntry);
        using EndListener = void (*)(Map* map);
        void SetEndListener(EndListener listener);

        // Unit::Kill, a creature died in an instance.
        void OnCreatureDeath(Creature* victim);
    }
}

#endif
