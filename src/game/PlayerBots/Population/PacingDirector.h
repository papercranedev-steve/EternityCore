/*
 * World population bots: pacing (ambient flow doc .kiro/docs/2026-11-22-ambient-flow.md §7,
 * user 2026-11-22: walking a long road and meeting nobody; an AI director for the rhythm).
 *
 * A small director in the manner of Left 4 Dead's: it does not place anyone itself, it tells the
 * field and traveller directors how fast and how many, per observer.
 *  - tension (0..1) from how he is doing: in combat (more with more attackers), low health,
 *    dead; it falls off with a half-life after the fight;
 *  - quiet: how long no population bot was within his sight;
 *  - relax: while tense and RelaxSec after it, nobody new comes (no one walks up mid-fight or
 *    while he eats);
 *  - spaced: a traveller sent for him less than MinGapSec ago, the next one waits (no crowd of
 *    travellers one after the other);
 *  - build-up: the longer the quiet, the more often travellers come and the more field bots
 *    around him (up to MaxBoost x the rate, MAX_DENSITY x the budget, from QuietSec / 3 to
 *    QuietSec);
 *  - due: quiet for QuietSec while walking: the traveller director sends someone now (if a road
 *    of his side is near).
 * Config PopBot.Pacing.* (mangosd.conf.dist.in). Arriving observers are not paced (their prewarm
 * fill is the whole picture at once). World thread only.
 */

#ifndef MANGOS_POPULATION_PACING_H
#define MANGOS_POPULATION_PACING_H

#include "Common.h"
#include <unordered_map>

namespace Population
{
    class PopulationMgr;
    class Observers;
    struct Observer;

    // What the directors do for one observer now.
    struct Pace
    {
        float rate = 1.0f;      // times the traveller arrival rate (and field check speed)
        float density = 1.0f;   // times the field budget
        bool relax = false;     // nobody new
        bool spaced = false;    // no new traveller yet
        bool due = false;       // a traveller now
    };

    class PacingDirector
    {
    public:
        // At or above this tension he is in the thick of it.
        static constexpr float PEAK_TENSION = 0.45f;
        static constexpr float TENSION_HALF_LIFE_SEC = 12.0f;
        static constexpr float MAX_DENSITY = 2.0f;
        static constexpr uint32 TICK_MS = 1 * IN_MILLISECONDS;

        struct State
        {
            float tension = 0.0f;
            uint32 peakMs = 0;          // last time at PEAK_TENSION, 0 = never
            uint32 quietSinceMs = 0;    // last time a population bot was within sight
            bool inSight = false;
            uint32 sentMs = 0;          // last traveller sent for him, 0 = never
            uint32 seenMs = 0;          // last update with him online
        };

        void LoadConfig();
        void Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs);

        bool IsEnabled() const { return m_enabled; }
        Pace For(Observer const& o) const;
        // A traveller was queued for him (spaces the next one).
        void NoteSent(uint32 observerGuid);
        State const* Find(uint32 observerGuid) const;
        uint32 QuietMs(State const& s) const;

        uint32 GetQuietMs() const { return m_quietMs; }
        uint32 GetMinGapMs() const { return m_minGapMs; }
        uint32 GetRelaxMs() const { return m_relaxMs; }
        float GetMaxBoost() const { return m_maxBoost; }

    private:
        static float TargetTension(Observer const& o);

        bool m_enabled = true;
        uint32 m_quietMs = 180 * IN_MILLISECONDS;
        uint32 m_minGapMs = 40 * IN_MILLISECONDS;
        uint32 m_relaxMs = 30 * IN_MILLISECONDS;
        float m_maxBoost = 4.0f;

        std::unordered_map<uint32, State> m_states;     // observer guid -> state
        uint32 m_nowMs = 0;
        uint32 m_nextTickMs = 0;
    };
}

#endif
