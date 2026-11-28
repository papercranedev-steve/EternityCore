/*
 * Herod boss module (header comment: script, match, rest, limits).
 */

#include "Commander/Encounters/ScarletHerod.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        bool HasTrainee(WorldModel const& world)
        {
            return std::any_of(world.enemies.begin(), world.enemies.end(), [](EnemyFacts const& e)
            {
                return e.summoned && e.entry == ScarletHerod::NPC_TRAINEE;
            });
        }
    }

    bool ScarletHerod::InWait(uint32 nowMs) const
    {
        if (m_diedAtMs)
            return WorldTimer::getMSTimeDiff(m_diedAtMs, nowMs) < TRAINEE_WAIT_MS;
        return !m_evaded && m_lastSeenMs && m_lastPct <= DEATH_PCT &&
               WorldTimer::getMSTimeDiff(m_lastSeenMs, nowMs) < TRAINEE_WAIT_MS;
    }

    bool ScarletHerod::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        return EncounterHelpers::HasEnemyEntry(world, NPC_HEROD) || HasTrainee(world) || InWait(world.refreshedAtMs);
    }

    void ScarletHerod::OnReset()
    {
        *this = ScarletHerod();
    }

    void ScarletHerod::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;
        if (EnemyFacts const* herod = EncounterHelpers::FindEnemyByEntry(world, NPC_HEROD))
        {
            m_herod = herod->guid;
            m_lastPct = herod->healthPct;
            m_lastSeenMs = now;
            if (!m_startNoted)
            {
                m_startNoted = true;
                Note("Herod: keep him within 32 yd of the armory's centre, or his 4 elite Myrmidons at the door join. "
                     "When he dies 20 trainees run in: nobody sits down.");
            }
        }
        else if (!m_diedAtMs && !m_evaded && !m_herod.IsEmpty() && bot && bot->me->IsInWorld())
        {
            // Gone from the enemies: dead (the trainees come) or evaded.
            Creature const* herod = bot->me->GetMap()->GetCreature(m_herod);
            if (herod && !herod->IsAlive())
            {
                m_diedAtMs = now;
                Note("Herod died: his trainees are coming, nobody sits down.");
                Say(EncounterHelpers::Line("voice.boss.herod.trainees", VoiceTier::Urgent));
            }
            else
                m_evaded = true;
        }

        m_noRest = HasTrainee(world) || InWait(now);
    }
}
