/*
 * World population bots, field: where people level in the open world. Every creature spawn
 * point of a continent (`creature` table) of a creature a player would hunt: normal rank, no
 * guard, civilian, critter, totem or service NPC, no stable mount, no XP-less or unkillable
 * creature (training dummies, town NPCs). A field bot logs in next to one, out of
 * everyone's sight, and fights what lives there (Commander hunting, HuntAssigner).
 * Built per map on first use from ObjectMgr, kept in CELL_YD cells. World thread only.
 */

#ifndef MANGOS_POPULATION_FIELD_SPOTS_H
#define MANGOS_POPULATION_FIELD_SPOTS_H

#include "Common.h"
#include <unordered_map>
#include <vector>

namespace Population
{
    struct FieldSpot
    {
        uint32 entry = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        uint8 levelMin = 1, levelMax = 1;
        uint32 faction = 0;         // faction template
    };

    class FieldSpots
    {
    public:
        static constexpr float CELL_YD = 64.0f;

        // Spots of `mapId` from minYd to maxYd (2d) of x/y.
        std::vector<FieldSpot const*> Around(uint32 mapId, float x, float y, float minYd, float maxYd);

    private:
        struct MapSpots
        {
            std::vector<FieldSpot> spots;
            std::unordered_map<uint64, std::vector<uint32>> cells;
        };
        MapSpots const& Of(uint32 mapId);

        std::unordered_map<uint32, MapSpots> m_maps;
    };
}

#endif
