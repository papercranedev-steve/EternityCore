/*
 * PartyBot Commander boss module: Belnistrasz's idol ritual, Razorfen Downs (boss doc 2.3, P3).
 *
 * Script (razorfen_downs.cpp npc_belnistraszAI): the quest NPC walks to the idol, then stands
 * channelling Idol Shutdown (12774) for about 4 minutes while six waves spawn at one of three
 * spawner points and run at him: five groups of 2 Withered Battle Boar + 1 Withered Quilguard
 * + 1 Death's Head Geomancer, then Plaguemaw the Rotting alone. The waves are on the script's
 * clock (40 s / 20 s apart, not kill-gated); if he dies the quest fails.
 *
 * He is an escort (npc_escortAI escorting), so the generic protect (boss doc G3) already heals
 * him, kills his attackers first and takes them off him. This module adds only the wave
 * schedule (WorldModel::waves) so the mana budget counts the waves still to come and bots drink
 * in the gaps (Coordination/Situation/WaveRest.h), and a start note / voice line.
 *
 * Match: map 129 and a summoned wave creature among the enemies (Matches sees only enemies, so
 *   the first wave turns it on); then kept on while Belnistrasz (8516, found live: between waves
 *   he is out of combat and not in WorldModel::allies) carries Idol Shutdown.
 *
 * Timeline (EncounterHelpers::WaveClock): the script's fixed timers from the aura going up. The
 * zero is set when the module turns on, back-dated from the first wave creature's first sighting
 * (EnemyFacts::firstSeenMs) minus its spawn and SPAWN_TRAVEL_SEC.
 *
 * Known limits:
 *   - The ritual phase is not exposed by the instance; a server that retimes the ritual drifts.
 *   - Waves 1-5 have the same creatures: a module turning on after wave 1 (a reload, a party
 *     arriving late) takes them for wave 1.
 *   - No positioning: where to stand to meet the waves is the generic modules'.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_RAZORFEN_BELNISTRASZ_H
#define MANGOS_COMMANDER_ENCOUNTER_RAZORFEN_BELNISTRASZ_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"

namespace Commander
{
    class RazorfenBelnistrasz : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 129;
        static constexpr uint32 NPC_BELNISTRASZ = 8516;
        static constexpr uint32 SPELL_IDOL_SHUTDOWN = 12774;
        // Wave creatures (razorfen_downs.cpp).
        static constexpr uint32 NPC_BATTLE_BOAR = 7333;
        static constexpr uint32 NPC_QUILGUARD = 7329;
        static constexpr uint32 NPC_GEOMANCER = 7335;
        static constexpr uint32 NPC_PLAGUEMAW = 7356;

        char const* GetId() const override { return "rfd-belnistrasz"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

    private:
        bool RitualOn(TeamBotAI* bot) const;

        EncounterHelpers::WaveClock m_clock;
        bool m_active = false;          // ritual on or a wave creature left
        uint32 m_wavesArrived = 0;
        uint32 m_wavesLeft = 0;
        float m_nextArrivalSec = -1.0f;
        bool m_startNoted = false;
    };
}

#endif
