/*
 * World population bots, phase 1b: city crowds.
 *
 * While an observer (a real player, or one about to log in there) is in a city zone, the
 * director keeps the city at its target size:
 * stand bots at auctioneers / bankers / mailboxes, movers running between those spots, and a
 * few geared veterans. Anchors come from the world database (NPCs with the auctioneer or
 * banker flag and mailboxes near the city centre, current patch only) plus manual scene
 * points recorded with .popbot point add. World thread only.
 */

#ifndef MANGOS_POPULATION_CITY_H
#define MANGOS_POPULATION_CITY_H

#include "Common.h"
#include "Population/PopulationTypes.h"
#include <string>
#include <vector>

class Map;

namespace Population
{
    class PopulationMgr;
    class ScenePoints;
    class Observers;

    struct CityDef
    {
        uint32 zone;
        char const* key;        // same names as scene points
        char const* title;
        uint32 mapId;
        float centerX, centerY, radius;
        uint32 team;
    };

    struct CityAnchor
    {
        std::string type;       // ah bank mail idle showcase, and PlaceAnchors.h types (only visited)
        std::string label;      // NPC name or "point #id"
        float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        bool exactSpot = false; // manual point: stand exactly here, facing o
        uint32 capacity = 1;
        bool object = false;    // a gameobject (mailbox, chair, anvil, fire)
        uint32 chairEntry = 0;  // chair: its gameobject entry
    };

    struct CityState
    {
        CityDef const* def = nullptr;
        bool enabled = false;               // listed in PopBot.City.Zones
        std::vector<CityAnchor> anchors;
        PlaceMenu places;                   // the anchors, for the crowd's day (TownLife.h)
        bool playerPresent = false;         // an observer (Observers.h) is in the city
        bool arriving = false;              // one of them is about to enter the world here: fill at once
        uint32 lastPlayerSeenMs = 0;
        bool everSeen = false;
    };

    // Bots of one city or town, including queued requests.
    struct CityCounts
    {
        uint32 total = 0, stand = 0, mover = 0, showcase = 0, crafter = 0, sitter = 0;
        std::vector<uint32> anchorUse;      // stand + showcase bots per anchor
    };

    struct CityTargets
    {
        uint32 total = 0, mover = 0, showcase = 0, crafter = 0;
    };

    class CityDirector
    {
    public:
        static std::vector<CityDef> const& Cities();
        static CityDef const* FindCityByKey(std::string const& key);

        void LoadConfig();
        // Re-reads DB anchors and adds the manual scene points.
        void LoadAnchors(ScenePoints const& points);
        void Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs);
        // Run the next update right away (.popbot fill).
        void ForceTick() { m_nextTickMs = 0; }

        bool IsEnabled() const { return m_enabled; }
        float HourFactor() const;
        CityTargets TargetsNow() const;
        std::vector<CityState> const& States() const { return m_states; }
        CityState const* FindState(uint32 zone) const;

        // A walkable spot of the city near (x, y, z) that no player in the world can see, to log
        // in at and walk from, or to walk to and log out. Known spots first (anchors, scene points),
        // then the ground around. False: none (the bot should not appear / vanish now).
        bool PickHiddenSpot(uint32 zone, float x, float y, float z, Observers const& observers, Waypoint& out) const;

        // Stand spot in front of an anchor (ground height and line of sight checked on `map`).
        static bool PickStandSpot(Map* map, CityAnchor const& anchor, float minDist, float maxDist, float spread, Waypoint& out);

    private:
        void TickCity(PopulationMgr& mgr, CityState& city, uint32 nowMs);
        bool BuildRequest(CityState const& city, CityRole role, CityCounts const& counts, SpawnRequest& out) const;

        bool m_enabled = true;
        uint32 m_maxBots = 40;
        uint32 m_maxMoving = 12;
        uint32 m_showcase = 4;
        uint32 m_crafters = 6;
        uint32 m_emptyGraceMs = 120 * IN_MILLISECONDS;
        std::vector<uint32> m_zones;
        float m_hourCurve[24] = {};

        std::vector<CityState> m_states;
        uint32 m_nextTickMs = 0;
    };
}

#endif
