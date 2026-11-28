/*
 * Belnistrasz idol ritual module (header comment: script, match, timeline, limits).
 */

#include "Commander/Encounters/RazorfenBelnistrasz.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Seconds from the Idol Shutdown aura going up to each wave spawning
        // (razorfen_downs.cpp UpdateEscortAI: the aura at ritual phase 0, t = 1 s; waves at
        // t = 2, 41, 81, 121, 161, Plaguemaw at 221). Six waves; the sixth is Plaguemaw alone.
        constexpr float SPAWN_SEC[] = { 1.0f, 40.0f, 80.0f, 120.0f, 160.0f, 220.0f };
        constexpr uint32 WAVE_COUNT = 6;
        // The spawner's creatures appear 8-15 yd from him and walk in (untested guess).
        constexpr float SPAWN_TRAVEL_SEC = 4.0f;
        constexpr float INTERVAL_SEC = 40.0f;
        // He stands at the idol; the party is near him.
        constexpr float SEARCH_YD = 100.0f;

        // Boars, Quilguards and Geomancers come in waves 1-5 (counted as the first when the
        // module turns on late), Plaguemaw is the sixth.
        int WaveOf(EnemyFacts const& e)
        {
            if (!e.summoned)
                return -1;
            switch (e.entry)
            {
                case RazorfenBelnistrasz::NPC_BATTLE_BOAR:
                case RazorfenBelnistrasz::NPC_QUILGUARD:
                case RazorfenBelnistrasz::NPC_GEOMANCER:
                    return 0;
                case RazorfenBelnistrasz::NPC_PLAGUEMAW:
                    return int(WAVE_COUNT) - 1;
                default:
                    return -1;
            }
        }
    }

    bool RazorfenBelnistrasz::RitualOn(TeamBotAI* bot) const
    {
        if (!bot || !bot->me->IsInWorld())
            return false;
        // Live: between waves he is out of combat, so not in WorldModel::allies.
        Creature const* belnistrasz = bot->me->FindNearestCreature(NPC_BELNISTRASZ, SEARCH_YD);
        return belnistrasz && belnistrasz->HasAura(SPELL_IDOL_SHUTDOWN);
    }

    bool RazorfenBelnistrasz::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        // (Matches runs before Refresh and sees only enemies: the first wave turns the module
        // on, the aura keeps it on through the gaps.)
        if (m_active)
            return true;
        return std::any_of(world.enemies.begin(), world.enemies.end(),
                           [](EnemyFacts const& e) { return WaveOf(e) >= 0; });
    }

    void RazorfenBelnistrasz::OnReset()
    {
        *this = RazorfenBelnistrasz();
    }

    void RazorfenBelnistrasz::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;
        bool const ritual = RitualOn(bot);
        bool const waveLeft = std::any_of(world.enemies.begin(), world.enemies.end(),
                                          [](EnemyFacts const& e) { return WaveOf(e) >= 0; });
        m_active = ritual || waveLeft;
        if (!ritual)
        {
            // Over (or he died): the waves left alive are a plain fight.
            m_clock.Clear();
            m_wavesArrived = 0;
            m_wavesLeft = 0;
            m_nextArrivalSec = -1.0f;
            return;
        }

        m_clock.Start(world, now, SPAWN_SEC, WAVE_COUNT, SPAWN_TRAVEL_SEC, &WaveOf);
        m_clock.State(now, SPAWN_SEC, WAVE_COUNT, SPAWN_TRAVEL_SEC, m_wavesArrived, m_wavesLeft, m_nextArrivalSec);
        if (!m_startNoted)
        {
            m_startNoted = true;
            Note("Belnistrasz's ritual: six waves over about four minutes, 40 s apart; keep him alive. "
                 "Drink between waves, stand up before the next one.");
            Say(EncounterHelpers::Line("voice.boss.belnistrasz.ritual", VoiceTier::Essential));
        }
    }

    void RazorfenBelnistrasz::DescribeWaves(WaveEventFacts& out) const
    {
        if (!m_clock.zeroMs)
            return;
        out.active = true;
        out.pace = WavePace::Timer;
        out.wavesArrived = m_wavesArrived;
        out.wavesLeft = m_wavesLeft;
        out.nextArrivalSec = m_nextArrivalSec;
        out.intervalSec = INTERVAL_SEC;
    }
}
