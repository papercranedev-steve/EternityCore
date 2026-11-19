/*
 * World population bots, phase 1c: people levelling in the open world (design
 * .kiro/docs/2026-11-01-world-population-bots.md section 6, user 2026-11-18: never appear or
 * vanish in front of the player).
 *
 * Around every observer (Observers.h: a player in the world, or one about to log in there) in
 * the open world of a continent, not in a city, not on a flight path, the director keeps its
 * budget of field bots within PopBot.Field.Radius. The budget falls off with the distance from
 * the settlements of his side (user 2026-11-19: towns busy, the ring around them more hunters,
 * the wild few): density = floor + (1 - floor) * Settlements::Pull, budget = density x
 * PopBot.Field.MaxUnits; spots nearer a settlement are picked first more often.
 *  - a new one logs in at a creature spawn point (FieldSpots): ahead of a moving observer,
 *    just beyond the edge of his sight where he is heading (user 2026-11-19: he walks on a
 *    little and sees them), else (standing, or nothing ahead) between the spawn ring's inner
 *    edge (out of the map's visibility distance) and its outer edge; where no player can see
 *    it, now or after the login (Observers::CanSeeSoon), next to creatures that are not
 *    friendly to the observer, at a
 *    level that fits them (Commander HuntAssigner's window), from the observer's roster;
 *  - it is a team of one for the Commander (Commander/Team.h unit): it hunts, fights, rests,
 *    loots (PopulationBotAI field tick);
 *  - it leaves, only where nobody sees it, when its lifetime is over, when it is done here
 *    (BotHandle::done: enough fights, nothing left to hunt), or when every observer is farther
 *    than PopBot.Field.KeepYd.
 *    Bots far behind a moving observer (BEHIND_SPENT_YD) no longer count for him.
 * One unit per check and observer, checks every PopBot.Field.CheckMin..MaxSec (moving:
 * MOVING_CHECK_MIN..MAX_MS); an arriving observer is filled at once (prewarm). World thread only.
 */

#ifndef MANGOS_POPULATION_FIELD_H
#define MANGOS_POPULATION_FIELD_H

#include "Common.h"
#include "Population/FieldSpots.h"
#include "Population/PopulationTypes.h"
#include <unordered_map>

namespace Population
{
    class PopulationMgr;
    class Observers;
    class Settlements;
    struct Observer;

    class FieldDirector
    {
    public:
        // A login takes a moment: a spot must stay out of sight of a moving observer this long.
        static constexpr float LOGIN_LEAD_SEC = 3.0f;
        // Ahead of a moving observer: spots within this angle of his heading (each side), from
        // his sight's edge (+ LOGIN_LEAD_SEC of his way) to AHEAD_DEPTH_YD beyond it.
        static constexpr float AHEAD_HALF_ANGLE = float(M_PI) / 4.0f;
        static constexpr float AHEAD_DEPTH_YD = 40.0f;
        // Passed by: a field bot this far behind a moving observer no longer counts for him.
        static constexpr float BEHIND_SPENT_YD = 150.0f;
        // A moving observer is checked this often (people ahead are used up quickly).
        static constexpr uint32 MOVING_CHECK_MIN_MS = 5 * IN_MILLISECONDS;
        static constexpr uint32 MOVING_CHECK_MAX_MS = 10 * IN_MILLISECONDS;

        void LoadConfig();
        void Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs);

        bool IsEnabled() const { return m_enabled; }
        uint32 GetMaxUnits() const { return m_maxUnits; }
        // How busy the field is at x/y for `team` (PopBot.Field.FloorPct .. 1), and the field
        // bots wanted around an observer there (density x PopBot.Field.MaxUnits, rounded).
        float DensityAt(Settlements const& settlements, uint32 mapId, float x, float y, uint32 team) const;
        uint32 BudgetAt(Settlements const& settlements, uint32 mapId, float x, float y, uint32 team) const;
        float GetRadius() const { return m_radius; }
        float GetKeepYd() const { return m_keepYd; }

    private:
        // The open world of a continent around `o`, not a city, not flying.
        bool Fills(Observer const& o) const;
        bool BuildRequest(Observer const& o, Observers const& observers, Settlements const& settlements, SpawnRequest& out);
        // Creature spawn points to log in at: ahead of a moving observer, else the ring; busier
        // spots (DensityAt) first more often.
        std::vector<FieldSpot const*> CandidateSpots(Observer const& o, float visibleYd, Settlements const& settlements);
        void PreferBusySpots(std::vector<FieldSpot const*>& spots, Observer const& o, Settlements const& settlements) const;

        bool m_enabled = true;
        uint32 m_maxUnits = 8;          // at a settlement's edge (weight 1)
        float m_floor = 0.15f;          // density in the wild
        float m_falloffScale = 1.0f;    // times every settlement's falloff length
        float m_radius = 250.0f;
        float m_spawnMinYd = 130.0f;
        float m_spawnMaxYd = 240.0f;
        float m_keepYd = 320.0f;
        uint32 m_checkMinMs = 15 * IN_MILLISECONDS;
        uint32 m_checkMaxMs = 45 * IN_MILLISECONDS;

        FieldSpots m_spots;
        std::unordered_map<uint32, uint32> m_nextCheckMs;     // observer guid -> time
        uint32 m_nextTickMs = 0;
    };
}

#endif
