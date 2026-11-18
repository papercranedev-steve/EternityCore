/*
 * World population bots, phase 1c: people levelling in the open world (design
 * .kiro/docs/2026-11-01-world-population-bots.md section 6, user 2026-11-18: never appear or
 * vanish in front of the player).
 *
 * Around every observer (Observers.h: a player in the world, or one about to log in there) in
 * the open world of a continent, not in a city, not on a flight path, the director keeps up to
 * PopBot.Field.MaxUnits field bots within PopBot.Field.Radius:
 *  - a new one logs in at a creature spawn point (FieldSpots) between the spawn ring's inner
 *    edge (out of the map's visibility distance) and its outer edge, where no player can see
 *    it (Observers::CanSee), next to creatures that are not friendly to the observer, at a
 *    level that fits them (Commander HuntAssigner's window), from the observer's roster;
 *  - it is a team of one for the Commander (Commander/Team.h unit): it hunts, fights, rests,
 *    loots (PopulationBotAI field tick);
 *  - it leaves, only where nobody sees it, when its lifetime is over, when it is done here
 *    (BotHandle::done: enough fights, nothing left to hunt), or when every observer is farther
 *    than PopBot.Field.KeepYd.
 * One unit per check and observer, checks every FIELD_CHECK_MIN..MAX s; an arriving observer
 * is filled at once (prewarm). World thread only.
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
    struct Observer;

    class FieldDirector
    {
    public:
        void LoadConfig();
        void Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs);

        bool IsEnabled() const { return m_enabled; }
        uint32 GetMaxUnits() const { return m_maxUnits; }
        float GetRadius() const { return m_radius; }
        float GetKeepYd() const { return m_keepYd; }

    private:
        // The open world of a continent around `o`, not a city, not flying.
        bool Fills(Observer const& o) const;
        bool BuildRequest(Observer const& o, Observers const& observers, SpawnRequest& out);

        bool m_enabled = true;
        uint32 m_maxUnits = 4;
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
