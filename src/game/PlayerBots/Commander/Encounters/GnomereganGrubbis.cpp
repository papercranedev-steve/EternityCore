/*
 * Grubbis event module (header comment: script, match).
 */

#include "Commander/Encounters/GnomereganGrubbis.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "InstanceData.h"

namespace Commander
{
    bool GnomereganGrubbis::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        if (m_active)
            return true;
        return EncounterHelpers::HasEnemyEntry(world, NPC_GRUBBIS);
    }

    void GnomereganGrubbis::OnReset()
    {
        *this = GnomereganGrubbis();
    }

    void GnomereganGrubbis::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        bool inProgress = false;
        if (bot && bot->me->IsInWorld())
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
                inProgress = data->GetData(DATA_GRUBBIS) == STATE_IN_PROGRESS;
        m_active = inProgress || EncounterHelpers::HasEnemyEntry(world, NPC_GRUBBIS);

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Grubbis event: keep Emi alive. Ambushers keep coming until Grubbis, with only "
                 "short breaks, so nobody sits down until it is over.");
        }
    }

    void GnomereganGrubbis::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Continuous;
        out.wavesLeft = 1;
    }
}
