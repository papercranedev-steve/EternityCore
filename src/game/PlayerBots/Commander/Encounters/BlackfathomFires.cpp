/*
 * Fires of Aku'mai module (header comment: script, match, limits).
 */

#include "Commander/Encounters/BlackfathomFires.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "InstanceData.h"
#include <algorithm>

namespace Commander
{
    bool BlackfathomFires::IsWaveEntry(uint32 entry)
    {
        return entry == NPC_SNAPJAW || entry == NPC_SERVANT ||
               entry == NPC_SNAPCLAW || entry == NPC_SOFTSHELL;
    }

    bool BlackfathomFires::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        if (m_active)
            return true;
        for (EnemyFacts const& e : world.enemies)
            if (e.summoned && IsWaveEntry(e.entry))
                return true;
        return false;
    }

    void BlackfathomFires::OnReset()
    {
        *this = BlackfathomFires();
    }

    void BlackfathomFires::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        bool inProgress = false;
        if (bot && bot->me->IsInWorld())
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
                inProgress = data->GetData(DATA_SHRINE) == STATE_IN_PROGRESS;
        bool const waveLeft = std::any_of(world.enemies.begin(), world.enemies.end(),
            [](EnemyFacts const& e) { return e.summoned && IsWaveEntry(e.entry); });
        m_active = inProgress || waveLeft;

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Fires of Aku'mai: each brazier you light sends one wave. Clear it, drink up, "
                 "then light the next; we will say when we are ready.");
        }
    }

    void BlackfathomFires::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Player;
        // The player controls the pace; the count is not exposed. One wave left is enough for
        // the rest rule to treat it as a player-paced event (drink to full between waves).
        out.wavesLeft = 1;
    }
}
