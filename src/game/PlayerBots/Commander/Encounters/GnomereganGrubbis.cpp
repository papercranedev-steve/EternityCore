/*
 * Grubbis event module (header comment: script, match).
 */

#include "Commander/Encounters/GnomereganGrubbis.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include "InstanceData.h"
#include <algorithm>

namespace Commander
{
    bool GnomereganGrubbis::IsEventEntry(EnemyFacts const& e)
    {
        // Ambushers and Burrowers also stand in the caves: only Emi's summoned packs count.
        return e.entry == NPC_GRUBBIS || (e.summoned && (e.entry == NPC_AMBUSHER || e.entry == NPC_BURROWER));
    }

    bool GnomereganGrubbis::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        // (Matches runs before Refresh: the instance state keeps it on through the lulls once
        // the first pack made it active.)
        if (m_active)
            return true;
        return std::any_of(world.enemies.begin(), world.enemies.end(), IsEventEntry);
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
        m_active = inProgress || std::any_of(world.enemies.begin(), world.enemies.end(), IsEventEntry);

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
