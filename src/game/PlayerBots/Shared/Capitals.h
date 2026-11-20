/*
 * Shared bot facts (PlayerBots/Shared): the six capital cities, where a bot finds every service
 * of its faction (user 2026-11-20: in a capital the class trainer is worth a walk across the
 * city, Life/Errands/Trip/TrainingPlanner). Centre and radius cover the city's streets.
 * (Population/CityPopulation keeps its own table with its city keys and names.)
 */

#ifndef MANGOS_BOTSHARED_CAPITALS_H
#define MANGOS_BOTSHARED_CAPITALS_H

#include "Common.h"
#include <vector>

namespace BotShared
{
    namespace Capitals
    {
        struct Capital
        {
            uint32 zoneId;
            uint32 mapId;
            float x, y;
            float radius;
            uint32 team;    // ALLIANCE / HORDE
        };

        std::vector<Capital> const& All();
        // The capital of this zone, or null.
        Capital const* OfZone(uint32 zoneId);
    }
}

#endif
