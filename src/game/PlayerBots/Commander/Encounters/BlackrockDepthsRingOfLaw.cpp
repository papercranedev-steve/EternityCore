/*
 * Ring of Law module (header comment: script, match, waves).
 */

#include "Commander/Encounters/BlackrockDepthsRingOfLaw.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "InstanceData.h"
#include <algorithm>

namespace Commander
{
    bool BlackrockDepthsRingOfLaw::IsRingEntry(uint32 entry)
    {
        switch (entry)
        {
            // Beasts (blackrock_depths.cpp RingMob).
            case 8925: case 8926: case 8927: case 8928: case 8932: case 8933:
            // Bosses (RingBoss).
            case 9027: case 9028: case 9029: case 9030: case 9031: case 9032:
            // Theldren's band (SummonRingBoss, the banner challenge).
            case 16049: case 16050: case 16051: case 16052: case 16053: case 16054:
            case 16055: case 16058: case 16059:
                return true;
            default:
                return false;
        }
    }

    bool BlackrockDepthsRingOfLaw::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        // (Matches runs before Refresh: the instance state keeps it on between the groups.)
        if (m_active)
            return true;
        return std::any_of(world.enemies.begin(), world.enemies.end(),
                           [](EnemyFacts const& e) { return IsRingEntry(e.entry); });
    }

    void BlackrockDepthsRingOfLaw::OnReset()
    {
        *this = BlackrockDepthsRingOfLaw();
    }

    void BlackrockDepthsRingOfLaw::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        bool inProgress = false;
        if (bot && bot->me->IsInWorld())
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
                inProgress = data->GetData(DATA_RING_OF_LAW) == STATE_IN_PROGRESS;
        bool const ringUp = std::any_of(world.enemies.begin(), world.enemies.end(),
                                        [](EnemyFacts const& e) { return IsRingEntry(e.entry); });
        m_active = inProgress || ringUp;
        m_watch.Update(world);
        m_quiet = m_watch.QuietMs(world.refreshedAtMs) >= QUIET_MS;

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Ring of Law: two groups of beasts, then a boss; nobody sits down until it is over.");
        }
    }

    void BlackrockDepthsRingOfLaw::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active || m_quiet)
            return;
        out.active = true;
        out.pace = WavePace::Continuous;
        out.wavesLeft = 1;
    }
}
