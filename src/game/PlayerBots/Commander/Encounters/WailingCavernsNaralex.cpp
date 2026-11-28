/*
 * Disciple of Naralex ritual module (header comment: script, match, limits).
 */

#include "Commander/Encounters/WailingCavernsNaralex.h"
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
        // Seconds from the Awakening aura going up to each wave spawning (wailing_caverns.cpp
        // waypoint 30: the aura at t = 2 s; Moccasins at t = 11, Ectoplasm at 51, Mutanus at 101).
        // Three waves; the third is Mutanus.
        constexpr float SPAWN_SEC[] = { 9.0f, 49.0f, 99.0f };
        constexpr uint32 WAVE_COUNT = 3;
        // The summons walk (Mutanus too) from the chamber's edges to him (untested guess).
        constexpr float SPAWN_TRAVEL_SEC = 5.0f;
        constexpr float INTERVAL_SEC = 40.0f;
        constexpr float SEARCH_YD = 100.0f;

        int WaveOf(EnemyFacts const& e)
        {
            if (!e.summoned)
                return -1;
            switch (e.entry)
            {
                case WailingCavernsNaralex::NPC_MOCCASIN:  return 0;
                case WailingCavernsNaralex::NPC_ECTOPLASM: return 1;
                case WailingCavernsNaralex::NPC_MUTANUS:   return 2;
                default:                                   return -1;
            }
        }
    }

    bool WailingCavernsNaralex::RitualOn(TeamBotAI* bot) const
    {
        if (!bot || !bot->me->IsInWorld())
            return false;
        // Live: between waves he is out of combat, so not in WorldModel::allies.
        Creature const* disciple = bot->me->FindNearestCreature(NPC_DISCIPLE, SEARCH_YD);
        return disciple && disciple->HasAura(SPELL_AWAKENING);
    }

    bool WailingCavernsNaralex::Matches(WorldModel const& world, uint32 mapId) const
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

    void WailingCavernsNaralex::OnReset()
    {
        *this = WailingCavernsNaralex();
    }

    void WailingCavernsNaralex::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;
        bool const ritual = RitualOn(bot);
        bool const waveLeft = std::any_of(world.enemies.begin(), world.enemies.end(),
                                          [](EnemyFacts const& e) { return WaveOf(e) >= 0; });
        m_active = ritual || waveLeft;
        if (!ritual)
        {
            // Over (Mutanus dead, the aura removed) or the disciple died.
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
            Note("Naralex's ritual: two waves of adds, then Mutanus; keep the disciple alive. "
                 "Drink between waves, and keep mana for Mutanus.");
            Say(EncounterHelpers::Line("voice.boss.naralex.ritual", VoiceTier::Essential));
        }
    }

    void WailingCavernsNaralex::DescribeWaves(WaveEventFacts& out) const
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
