/*
 * Tuten'kash gong module (header comment: script, match).
 */

#include "Commander/Encounters/RazorfenGong.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "InstanceData.h"
#include <algorithm>

namespace Commander
{
    bool RazorfenGong::IsWaveEntry(uint32 entry)
    {
        return entry == NPC_TOMB_FIEND || entry == NPC_TOMB_REAVER;
    }

    bool RazorfenGong::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        if (m_active)
            return true;
        for (EnemyFacts const& e : world.enemies)
            if (IsWaveEntry(e.entry))
                return true;
        return false;
    }

    void RazorfenGong::OnReset()
    {
        *this = RazorfenGong();
    }

    void RazorfenGong::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 stage = 0;
        if (bot && bot->me->IsInWorld())
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
                stage = data->GetData(DATA_GONG_WAVES);
        bool const waveLeft = std::any_of(world.enemies.begin(), world.enemies.end(),
            [](EnemyFacts const& e) { return IsWaveEntry(e.entry); });
        // Stage 15 is Tuten'kash himself: a plain boss fight, not a wave event.
        m_active = (stage >= 1 && stage <= 14) || waveLeft;

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Tuten'kash gong: ring it for each wave (fiends, then reavers, then Tuten'kash). "
                 "Clear the wave, drink up, then ring again; we will say when we are ready.");
        }
    }

    void RazorfenGong::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Player;
        out.wavesLeft = 1;
    }
}
