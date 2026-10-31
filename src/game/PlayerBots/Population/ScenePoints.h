/*
 * World population bots: city scene points (auction house, bank, routes, ...).
 * Stored in the world table `custom_popbot_point` (sql/custom/popbot.sql), recorded in game
 * with `.popbot point add`. World thread only.
 */

#ifndef MANGOS_POPULATION_SCENE_POINTS_H
#define MANGOS_POPULATION_SCENE_POINTS_H

#include "Common.h"
#include <string>
#include <vector>

namespace Population
{
    struct ScenePoint
    {
        uint32 id = 0;
        std::string city;
        std::string type;
        std::string route;      // only for type "route"
        uint32 seq = 0;         // order inside the route
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        uint32 capacity = 1;
    };

    class ScenePoints
    {
    public:
        // False if the table does not exist.
        bool Load();
        bool IsAvailable() const { return m_available; }

        std::vector<ScenePoint> const& All() const { return m_points; }
        ScenePoint const* Find(uint32 id) const;

        static bool IsValidCity(std::string const& city);
        static bool IsValidType(std::string const& type);
        static bool IsValidRouteName(std::string const& route);
        static char const* CityList();
        static char const* TypeList();

        // Inserts and reloads. Returns the new id, 0 on failure. Inputs must be validated.
        uint32 Add(ScenePoint const& point);
        bool Remove(uint32 id);
        // Next free sequence number of a route.
        uint32 NextSeq(std::string const& city, std::string const& route) const;

    private:
        std::vector<ScenePoint> m_points;
        bool m_available = false;
    };
}

#endif
