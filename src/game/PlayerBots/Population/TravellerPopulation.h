/*
 * World population bots: people on the roads (ambient flow doc .kiro/docs/2026-11-22-ambient-flow.md
 * §3.2, user 2026-11-22: GTA-like, people passing by).
 *
 * Roads: Commander/Perception/Spatial/RoadNetwork between the settlements (Settlements.h),
 * built in the background after each load (one road per update).
 *
 * Around every observer in the open world of a continent (as FieldDirector: not in a city, not
 * flying), the roads passing within NEAR_ROAD_YD bring people:
 *  - how busy a road is: a gravity model, PopBot.Road.PerHour x size(a) x size(b) /
 *    max(0.5, length / 1000 yd), times the hour curve (CityDirector::HourFactor);
 *  - when one comes: a Poisson process, the near roads' rates summed; each tick of TICK_MS a
 *    traveller comes with probability 1 - e^(-rate x tick). An observer about to log in gets
 *    Poisson(rate x the time to walk past him) at once, placed anywhere on those roads;
 *  - where: on the road just beyond his sight (ahead of him more often when he walks), walking
 *    towards him and on to the road's end; at most PopBot.Road.MaxNear within NEAR_COUNT_YD;
 *  - who: the observer's side (both ends of the road must serve it), at the levels of the two
 *    ends (TownState levels), from the roster or a stranger; from level 40 most ride their
 *    race's mount (PopBot.Road.MountPct).
 * It fights what attacks it (a team of one for the Commander, no hunting of its own:
 * TeamBotAI::SeeksFights) and leaves once out of sight after passing (PopulationMgr::RetireTravellers).
 * World thread only.
 */

#ifndef MANGOS_POPULATION_TRAVELLER_H
#define MANGOS_POPULATION_TRAVELLER_H

#include "Common.h"
#include "Commander/Perception/Spatial/RoadNetwork.h"
#include "Population/PopulationTypes.h"
#include <map>
#include <set>
#include <vector>

namespace Population
{
    class PopulationMgr;
    class Observers;
    class Settlements;
    class ScenePoints;
    class CityDirector;
    class TownDirector;
    struct Observer;

    class TravellerDirector
    {
    public:
        // Roads passing this close to an observer bring him people.
        static constexpr float NEAR_ROAD_YD = 150.0f;
        // Travellers counted around an observer (PopBot.Road.MaxNear).
        static constexpr float NEAR_COUNT_YD = 400.0f;
        // Logs in this far beyond the observer's sight (yards, along the road's points).
        static constexpr float SPAWN_BEYOND_MIN_YD = 20.0f;
        static constexpr float SPAWN_BEYOND_MAX_YD = 90.0f;
        // Out of sight and farther than the sight + this after having been seen: passed by.
        static constexpr float GONE_BEYOND_YD = 15.0f;
        static constexpr uint32 GONE_UNSEEN_MS = 5 * IN_MILLISECONDS;
        // Never seen by anyone this long: nobody came his way.
        static constexpr uint32 UNMET_MS = 120 * IN_MILLISECONDS;

        void LoadConfig();
        // New roads between `settlements` (patrol waypoints and the manual road points as hints).
        void BeginRoads(Settlements const& settlements, CityDirector const& cities, ScenePoints const& points);
        void Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs);

        bool IsEnabled() const { return m_enabled; }
        uint32 GetMaxNear() const { return m_maxNear; }
        Commander::Roads::RoadNetwork const& Roads() const { return m_roads; }
        // Travellers an hour on the road at hour factor 1 (gravity model).
        float PerHour(Commander::Roads::RoadEdge const& edge) const;
        // Travellers per second coming to `o` now (the near roads his side may use).
        float RateFor(Observer const& o, Settlements const& settlements, float hourFactor) const;

    private:
        // The open world of a continent around `o`, not a city, not flying.
        bool Fills(Observer const& o) const;
        bool UsableBy(Commander::Roads::RoadEdge const& edge, Settlements const& settlements, uint32 team) const;
        std::vector<Commander::Roads::RoadEdge const*> RoadsFor(Observer const& o, Settlements const& settlements) const;
        // One traveller on `edge` for `o`: beyond his sight walking towards him (anywhere on the
        // near part of the road when `placeAnywhere`: he is not in the world yet).
        bool BuildRequest(PopulationMgr& mgr, Observer const& o, Observers const& observers,
                          Commander::Roads::RoadEdge const& edge, bool placeAnywhere, SpawnRequest& out) const;
        void LevelsOf(PopulationMgr& mgr, Commander::Roads::RoadEdge const& edge, uint8& lo, uint8& hi) const;

        bool m_enabled = true;
        float m_perHour = 20.0f;
        uint32 m_maxNear = 4;
        uint32 m_mountPct = 60;

        Commander::Roads::RoadNetwork m_roads;
        std::vector<Commander::Roads::RoadHint> m_patrolHints;
        bool m_patrolLoaded = false;
        std::map<uint32, float> m_nodeSize;     // settlement id -> gravity size
        std::set<uint32> m_prewarmed;           // arriving observers given their travellers
        uint32 m_nextTickMs = 0;
    };
}

#endif
