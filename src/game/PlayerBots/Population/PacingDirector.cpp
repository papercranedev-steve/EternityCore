#include "Population/PacingDirector.h"
#include "Population/Observers.h"
#include "Population/PopulationMgr.h"
#include "Config/Config.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace Population
{
    namespace
    {
        uint32 SecToMs(int32 sec, int32 lo, int32 hi)
        {
            return uint32(std::min(std::max(sec, lo), hi)) * IN_MILLISECONDS;
        }

        // Forgotten after he is gone this long (relog keeps the state).
        uint32 const FORGET_MS = 10 * MINUTE * IN_MILLISECONDS;
    }

    void PacingDirector::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Pacing.Enable", true);
        m_quietMs = SecToMs(sConfig.GetIntDefault("PopBot.Pacing.QuietSec", 180), 30, 3600);
        m_minGapMs = SecToMs(sConfig.GetIntDefault("PopBot.Pacing.MinGapSec", 40), 0, 600);
        m_relaxMs = SecToMs(sConfig.GetIntDefault("PopBot.Pacing.RelaxSec", 30), 0, 600);
        m_maxBoost = float(std::min(std::max(sConfig.GetIntDefault("PopBot.Pacing.MaxBoost", 4), 1), 20));
    }

    float PacingDirector::TargetTension(Observer const& o)
    {
        if (o.dead)
            return 1.0f;
        float t = 0.0f;
        if (o.inCombat)
            t += 0.3f + 0.15f * float(std::min<uint32>(o.attackers, 4));
        float const LOW_HP = 0.7f;
        if (o.hpPct < LOW_HP)
            t += (LOW_HP - o.hpPct) / LOW_HP * 0.6f;
        return std::min(1.0f, t);
    }

    void PacingDirector::Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs)
    {
        if (nowMs < m_nextTickMs)
            return;
        float const dtSec = m_nowMs ? float(nowMs - m_nowMs) / float(IN_MILLISECONDS) : 0.0f;
        m_nowMs = nowMs;
        m_nextTickMs = nowMs + TICK_MS;
        if (!m_enabled)
        {
            m_states.clear();
            return;
        }

        float const decay = std::exp(-dtSec * std::log(2.0f) / TENSION_HALF_LIFE_SEC);
        for (Observer const& o : observers.All())
        {
            if (o.arriving)
                continue;
            auto inserted = m_states.emplace(o.guid, State());
            State& s = inserted.first->second;
            if (inserted.second)
                s.quietSinceMs = nowMs;
            s.seenMs = nowMs;

            s.tension = std::max(TargetTension(o), s.tension * decay);
            if (s.tension >= PEAK_TENSION)
                s.peakMs = nowMs;

            s.inSight = mgr.AnyoneInSight(o);
            if (s.inSight)
                s.quietSinceMs = nowMs;
        }

        for (auto it = m_states.begin(); it != m_states.end();)
            it = nowMs - it->second.seenMs > FORGET_MS ? m_states.erase(it) : std::next(it);
    }

    uint32 PacingDirector::QuietMs(State const& s) const
    {
        return s.inSight ? 0 : m_nowMs - s.quietSinceMs;
    }

    Pace PacingDirector::For(Observer const& o) const
    {
        Pace pace;
        if (!m_enabled || o.arriving)
            return pace;
        State const* s = Find(o.guid);
        if (!s)
            return pace;

        pace.relax = s->tension >= PEAK_TENSION || (s->peakMs && m_nowMs - s->peakMs < m_relaxMs);
        pace.spaced = s->sentMs && m_nowMs - s->sentMs < m_minGapMs;

        // Build-up from a third of the quiet time to all of it.
        uint32 const quiet = QuietMs(*s);
        uint32 const rampFrom = m_quietMs / 3;
        float const f = quiet <= rampFrom ? 0.0f : std::min(1.0f, float(quiet - rampFrom) / float(m_quietMs - rampFrom));
        pace.rate = 1.0f + (m_maxBoost - 1.0f) * f;
        pace.density = 1.0f + (MAX_DENSITY - 1.0f) * f;
        pace.due = !pace.relax && !pace.spaced && quiet >= m_quietMs && o.IsMoving();
        return pace;
    }

    void PacingDirector::NoteSent(uint32 observerGuid)
    {
        auto it = m_states.find(observerGuid);
        if (it != m_states.end())
            it->second.sentMs = m_nowMs;
    }

    PacingDirector::State const* PacingDirector::Find(uint32 observerGuid) const
    {
        auto it = m_states.find(observerGuid);
        return it != m_states.end() ? &it->second : nullptr;
    }
}
