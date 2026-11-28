/*
 * Belnistrasz idol ritual module (header comment: script, match, timeline, limits).
 */

#include "Commander/Encounters/RazorfenBelnistrasz.h"
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
        // Seconds from the Idol Shutdown aura going up to each wave spawning
        // (razorfen_downs.cpp UpdateEscortAI: 1 s shutdown, then 2, 41, 81, 121, 161, 221 s).
        // Six waves; the sixth is Plaguemaw alone.
        constexpr float SPAWN_SEC[] = { 2.0f, 41.0f, 81.0f, 121.0f, 161.0f, 221.0f };
        constexpr uint32 WAVE_COUNT = 6;
        // The spawner walks its creatures ~15 yd to him before they reach the party.
        constexpr float SPAWN_TRAVEL_SEC = 4.0f;
        constexpr float INTERVAL_SEC = 40.0f;
    }

    bool RazorfenBelnistrasz::IsWaveEntry(uint32 entry)
    {
        return entry == NPC_BATTLE_BOAR || entry == NPC_QUILGUARD ||
               entry == NPC_GEOMANCER || entry == NPC_PLAGUEMAW;
    }

    bool RazorfenBelnistrasz::RitualOn(TeamBotAI* bot, WorldModel const& world) const
    {
        if (!bot || !bot->me->IsInWorld())
            return false;
        // Belnistrasz is a friendly NPC near the party: an ally, not an enemy. Read him live.
        for (AllyFacts const& a : world.allies)
        {
            if (a.entry != NPC_BELNISTRASZ)
                continue;
            if (Creature const* c = bot->me->GetMap()->GetCreature(a.guid))
                return c->IsAlive() && c->HasAura(SPELL_IDOL_SHUTDOWN);
        }
        return false;
    }

    bool RazorfenBelnistrasz::Matches(WorldModel const& world, uint32 mapId) const
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

    void RazorfenBelnistrasz::OnReset()
    {
        *this = RazorfenBelnistrasz();
    }

    void RazorfenBelnistrasz::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;
        bool const ritual = RitualOn(bot, world);
        bool const waveLeft = std::any_of(world.enemies.begin(), world.enemies.end(),
            [](EnemyFacts const& e) { return e.summoned && IsWaveEntry(e.entry); });

        if (ritual && !m_ritualStartMs)
        {
            m_ritualStartMs = now;
            if (!m_startNoted)
            {
                m_startNoted = true;
                Note("Belnistrasz's ritual: six waves over about four minutes, 40 s apart; keep him alive. "
                     "Drink between waves, stand up before the next one.");
                Say(EncounterHelpers::Line("voice.boss.belnistrasz.ritual", VoiceTier::Essential));
            }
        }
        m_active = ritual || waveLeft;
        if (!m_active)
        {
            m_wavesArrived = 0;
            m_wavesLeft = 0;
            m_nextArrivalSec = -1.0f;
            return;
        }

        // Where the schedule stands (from the aura's start). Without the aura seen yet (a wave
        // creature is around but the ritual start was missed), treat it as the first wave on.
        float const elapsed = m_ritualStartMs ? WorldTimer::getMSTimeDiff(m_ritualStartMs, now) / 1000.0f : SPAWN_SEC[0];
        uint32 arrived = 0;
        float next = -1.0f;
        for (uint32 i = 0; i < WAVE_COUNT; ++i)
        {
            if (elapsed >= SPAWN_SEC[i])
                arrived = i + 1;
            else
            {
                next = SPAWN_SEC[i] - elapsed + SPAWN_TRAVEL_SEC;
                break;
            }
        }
        m_wavesArrived = arrived;
        m_wavesLeft = WAVE_COUNT - arrived;
        m_nextArrivalSec = next;
    }

    void RazorfenBelnistrasz::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_active)
            return;
        out.active = true;
        out.pace = WavePace::Timer;
        out.wavesArrived = m_wavesArrived;
        out.wavesLeft = m_wavesLeft;
        out.nextArrivalSec = m_nextArrivalSec;
        out.intervalSec = INTERVAL_SEC;
    }
}
