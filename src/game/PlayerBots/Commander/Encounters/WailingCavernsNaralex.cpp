/*
 * Disciple of Naralex ritual module (header comment: script, match, limits).
 */

#include "Commander/Encounters/WailingCavernsNaralex.h"
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
        // Seconds from the Awakening aura going up to each wave spawning (wailing_caverns.cpp
        // WaypointReached 30: 2 + 4 + 5 s of talk, then 12, 52, 102 s). Three waves; the third
        // is Mutanus.
        constexpr float SPAWN_SEC[] = { 12.0f, 52.0f, 102.0f };
        constexpr uint32 WAVE_COUNT = 3;
        // The summons run ~40 yd to him at run speed before they reach the party.
        constexpr float SPAWN_TRAVEL_SEC = 5.0f;
        constexpr float INTERVAL_SEC = 40.0f;
    }

    bool WailingCavernsNaralex::IsWaveEntry(uint32 entry)
    {
        return entry == NPC_MOCCASIN || entry == NPC_ECTOPLASM || entry == NPC_MUTANUS;
    }

    bool WailingCavernsNaralex::RitualOn(TeamBotAI* bot, WorldModel const& world) const
    {
        if (!bot || !bot->me->IsInWorld())
            return false;
        for (AllyFacts const& a : world.allies)
        {
            if (a.entry != NPC_DISCIPLE)
                continue;
            if (Creature const* c = bot->me->GetMap()->GetCreature(a.guid))
                return c->IsAlive() && c->HasAura(SPELL_AWAKENING);
        }
        return false;
    }

    bool WailingCavernsNaralex::Matches(WorldModel const& world, uint32 mapId) const
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

    void WailingCavernsNaralex::OnReset()
    {
        *this = WailingCavernsNaralex();
    }

    void WailingCavernsNaralex::Refresh(TeamBotAI* bot, WorldModel const& world)
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
                Note("Naralex's ritual: two waves of adds, then Mutanus; keep the disciple alive. "
                     "Drink between waves, and keep mana for Mutanus.");
                Say(EncounterHelpers::Line("voice.boss.naralex.ritual", VoiceTier::Essential));
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

    void WailingCavernsNaralex::DescribeWaves(WaveEventFacts& out) const
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
