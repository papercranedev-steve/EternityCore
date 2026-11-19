/*
 * World population bots: town residents (user 2026-11-19: Goldshire was empty; the inn should
 * be full, people at the smith, the town should look alive).
 *
 * Every settlement that is not a capital (Settlements.h: towns, start areas, camps, manual
 * hubs) has anchors from the world database within its core (+ ANCHOR_MARGIN_YD): chairs and
 * fires to sit at, the innkeeper, smiths (repair NPCs, anvils, forges), vendors, trainers,
 * mailboxes and flight masters to stand at. Game event spawns (Darkmoon Faire, Winter Veil)
 * are left out.
 * While an observer (Observers.h) is within ACTIVE_YD of a settlement's core, the director
 * keeps it at its target: residents by kind (RESIDENTS) x PopBot.Town.Scale x the hour curve
 * (CityDirector::HourFactor), at most what the anchors hold. A share of them sit (chairs
 * first), some walk between the anchors, the others stand. They come from the observer's
 * roster at the levels the creatures around the place have (any level when nobody fits),
 * log in out of sight and walk in when a player could see their spot (HiddenSpot.h), and
 * leave the same way. Nobody near for PopBot.City.EmptyGraceSec: the town empties.
 * World thread only.
 */

#ifndef MANGOS_POPULATION_TOWN_H
#define MANGOS_POPULATION_TOWN_H

#include "Common.h"
#include "Population/CityPopulation.h"
#include "Population/FieldSpots.h"
#include "Population/PopulationTypes.h"
#include <vector>

namespace Population
{
    class PopulationMgr;
    class Observers;
    class Settlements;
    struct Settlement;

    struct TownAnchor
    {
        CityAnchor spot;                    // types: chair fire inn smith vendor trainer quest mail flight
        bool object = false;                // a gameobject (stand all around it), else an NPC (in front)
        uint32 chairEntry = 0;              // type chair: its gameobject entry
    };

    struct TownState
    {
        Settlement const* settlement = nullptr;
        std::vector<TownAnchor> anchors;
        uint8 levelMin = 0, levelMax = 0;   // creatures around the place (0: none)
        bool playerPresent = false;
        bool arriving = false;
        uint32 observerTeam = 0;            // of the nearest observer (neutral towns)
        uint32 lastPlayerSeenMs = 0;
    };

    struct TownTargets
    {
        uint32 total = 0, sitter = 0, mover = 0;
    };

    class TownDirector
    {
    public:
        // From the core edge: an observer this close fills the town (before he can see it).
        static constexpr float ACTIVE_YD = 300.0f;
        // Anchors this far beyond the core still belong to the place (the smith at its edge).
        static constexpr float ANCHOR_MARGIN_YD = 30.0f;

        void LoadConfig();
        // Re-reads the anchors of every settlement (creature and gameobject data loaded).
        void LoadAnchors(Settlements const& settlements);
        void Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs);

        bool IsEnabled() const { return m_enabled; }
        std::vector<TownState> const& States() const { return m_states; }
        TownState const* FindState(uint32 settlementId) const;
        TownTargets TargetsFor(TownState const& town, float hourFactor) const;

        // A spot of the town near (x, y, z) nobody sees (HiddenSpot.h).
        bool PickHiddenSpot(uint32 settlementId, float x, float y, float z, Observers const& observers, Waypoint& out) const;

    private:
        void TickTown(PopulationMgr& mgr, TownState& town, float hourFactor, uint32 nowMs);
        bool BuildRequest(TownState const& town, CityRole role, CityCounts const& counts, SpawnRequest& out) const;

        bool m_enabled = true;
        float m_scale = 1.0f;
        uint32 m_emptyGraceMs = 120 * IN_MILLISECONDS;

        std::vector<TownState> m_states;
        FieldSpots m_spots;                 // creature levels around the places
        uint32 m_nextTickMs = 0;
    };
}

#endif
