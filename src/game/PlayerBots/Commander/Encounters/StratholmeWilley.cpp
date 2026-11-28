/*
 * Cannon Master Willey module (header comment: script, match).
 */

#include "Commander/Encounters/StratholmeWilley.h"
#include "Commander/Encounters/EncounterHelpers.h"

namespace Commander
{
    bool StratholmeWilley::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        return EncounterHelpers::HasEnemyEntry(world, NPC_WILLEY) ||
               EncounterHelpers::HasEnemyEntry(world, NPC_RIFLEMAN);
    }

    void StratholmeWilley::OnReset()
    {
        *this = StratholmeWilley();
    }

    void StratholmeWilley::Refresh(TeamBotAI* /*bot*/, WorldModel const& world)
    {
        // Active only while Willey is up: once he dies his riflemen despawn (EnterEvadeMode /
        // JustDied), so leftover riflemen alone do not keep the event on.
        m_active = EncounterHelpers::HasEnemyEntry(world, NPC_WILLEY);

        if (m_active && !m_startNoted)
        {
            m_startNoted = true;
            Note("Cannon Master Willey: riflemen keep coming every few seconds for the whole "
                 "fight; shoot them with the cannon. Nobody sits down until he is dead.");
        }
    }

    void StratholmeWilley::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Continuous;
        out.wavesLeft = 1;
    }
}
