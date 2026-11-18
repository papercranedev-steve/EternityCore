/*
 * Shared bot facts (PlayerBots/Shared): meeting points at town gates ("wait for me at the city
 * gate"). Soul companions doc §5 (stage S5, town trips).
 *
 * The world data has no gates, so they are recorded in game: ".partybot gate add [name]" where
 * the player stands (Companions/CompanionCommands.cpp). Characters table `custom_town_gate`
 * (map, zone, position, name). Thread safe: loaded at startup (CompanionMgr::Load), added and
 * removed by commands (world thread), read by bots (map threads).
 */

#ifndef MANGOS_BOTSHARED_TOWN_GATES_H
#define MANGOS_BOTSHARED_TOWN_GATES_H

#include "Common.h"
#include <string>
#include <vector>

namespace BotShared
{
    namespace TownGates
    {
        struct Gate
        {
            uint32 id = 0;
            uint32 mapId = 0;
            uint32 zoneId = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            std::string name;
        };

        // A town's gates are this close to where its business is done.
        constexpr float MAX_RANGE_YD = 800.0f;

        void Load();

        // The nearest gate of `zoneId` on `mapId` within MAX_RANGE_YD (2d) of x, y. False: none.
        bool Nearest(uint32 mapId, uint32 zoneId, float x, float y, Gate& out);

        // Stored at once. Returns the new gate's id (0: failed).
        uint32 Add(uint32 mapId, uint32 zoneId, float x, float y, float z, std::string const& name);
        // The nearest gate within `range` of x, y on `mapId`; false when none.
        bool RemoveNear(uint32 mapId, float x, float y, float range, Gate& removed);
        std::vector<Gate> OfZone(uint32 mapId, uint32 zoneId);
    }
}

#endif
