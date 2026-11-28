/*
 * Urok Doomhowl module (header comment: script, match).
 */

#include "Commander/Encounters/BlackrockSpireUrok.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include <algorithm>

namespace Commander
{
    bool BlackrockSpireUrok::IsUrokEntry(uint32 entry)
    {
        return entry == NPC_UROK || entry == NPC_ENFORCER || entry == NPC_OGRE_MAGUS;
    }

    bool BlackrockSpireUrok::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        return std::any_of(world.enemies.begin(), world.enemies.end(),
            [](EnemyFacts const& e) { return IsUrokEntry(e.entry); });
    }

    void BlackrockSpireUrok::OnReset()
    {
        *this = BlackrockSpireUrok();
    }

    void BlackrockSpireUrok::Refresh(TeamBotAI* /*bot*/, WorldModel const& world)
    {
        m_active = std::any_of(world.enemies.begin(), world.enemies.end(),
            [](EnemyFacts const& e) { return IsUrokEntry(e.entry); });

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Urok's challenge: ogres keep replacing each other until Urok appears, with no "
                 "break; nobody sits down until it is over.");
        }
    }

    void BlackrockSpireUrok::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Continuous;
        out.wavesLeft = 1;
    }
}
