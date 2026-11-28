/*
 * Tomb of the Seven module (header comment: script, match, waves).
 */

#include "Commander/Encounters/BlackrockDepthsSeven.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "InstanceData.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    bool BlackrockDepthsSeven::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        // (Matches runs before Refresh: the instance state keeps it on between dwarves.)
        if (m_active)
            return true;
        return std::any_of(world.enemies.begin(), world.enemies.end(),
                           [](EnemyFacts const& e) { return IsDwarf(e.entry); });
    }

    void BlackrockDepthsSeven::OnReset()
    {
        *this = BlackrockDepthsSeven();
    }

    void BlackrockDepthsSeven::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        m_nowMs = world.refreshedAtMs;
        bool inProgress = false;
        if (bot && bot->me->IsInWorld())
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
                inProgress = data->GetData(DATA_TOMB_OF_SEVEN) == STATE_IN_PROGRESS;

        bool dwarfUp = false;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!IsDwarf(e.entry))
                continue;
            dwarfUp = true;
            if (std::find(m_seen.begin(), m_seen.end(), e.guid) != m_seen.end())
                continue;
            m_seen.push_back(e.guid);
            uint32 const seenMs = e.firstSeenMs ? e.firstSeenMs : m_nowMs;
            m_lastCallMs = std::max(m_lastCallMs, seenMs);
        }
        m_active = inProgress || dwarfUp;

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Tomb of the Seven: a dwarf every 30 s whether the last is dead or not, seven in all. "
                 "Drinks only where a gap allows; Vile'rel heals, kill him first.");
        }
    }

    void BlackrockDepthsSeven::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Timer;
        uint32 const arrived = std::min<uint32>(DWARF_COUNT, uint32(m_seen.size()));
        out.wavesArrived = arrived;
        out.wavesLeft = DWARF_COUNT - arrived;
        out.intervalSec = CALL_INTERVAL_SEC;
        if (out.wavesLeft && m_lastCallMs)
        {
            float const since = WorldTimer::getMSTimeDiff(m_lastCallMs, m_nowMs) / 1000.0f;
            out.nextArrivalSec = std::max(0.0f, CALL_INTERVAL_SEC - since);
        }
    }
}
