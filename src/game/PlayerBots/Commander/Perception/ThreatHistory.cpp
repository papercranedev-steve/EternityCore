#include "Commander/Perception/ThreatHistory.h"
#include "Timer.h"

namespace Commander
{
    void ThreatHistory::Annotate(WorldModel& world, WorldModel const& previous, uint32 nowMs)
    {
        // Since when each enemy hits its current victim.
        for (EnemyFacts& e : world.enemies)
        {
            EnemyFacts const* prev = previous.FindEnemy(e.guid);
            e.victimSinceMs = (prev && prev->victim == e.victim) ? prev->victimSinceMs : nowMs;
            e.lastCastSpellId = e.castingSpellId ? e.castingSpellId : (prev ? prev->lastCastSpellId : 0);
        }

        while (!m_samples.empty() && WorldTimer::getMSTimeDiff(m_samples.front().timeMs, nowMs) > MAX_WINDOW_MS)
            m_samples.pop_front();

        // Newest sample that is old enough for a stable rate.
        Sample const* base = nullptr;
        for (auto itr = m_samples.rbegin(); itr != m_samples.rend(); ++itr)
        {
            if (WorldTimer::getMSTimeDiff(itr->timeMs, nowMs) >= MIN_WINDOW_MS)
            {
                base = &*itr;
                break;
            }
        }

        if (base)
        {
            float const seconds = WorldTimer::getMSTimeDiff(base->timeMs, nowMs) / 1000.0f;
            for (EnemyFacts& e : world.enemies)
            {
                for (ThreatEntry& t : e.threat)
                {
                    if (!t.partySide)
                        continue;
                    float old = 0.0f;   // not on the list back then = started from 0
                    for (Row const& r : base->rows)
                    {
                        if (r.enemy == e.guid && r.unit == t.guid)
                        {
                            old = r.threat;
                            break;
                        }
                    }
                    t.threatPerSec = (t.threat - old) / seconds;
                }
            }
        }

        Sample sample;
        sample.timeMs = nowMs;
        for (EnemyFacts const& e : world.enemies)
            for (ThreatEntry const& t : e.threat)
                if (t.partySide && t.onList)
                    sample.rows.push_back({ e.guid, t.guid, t.threat });
        m_samples.push_back(std::move(sample));
    }
}
