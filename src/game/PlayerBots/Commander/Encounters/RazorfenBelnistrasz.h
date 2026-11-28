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
 * Match: map 129 and Belnistrasz (8516) channelling Idol Shutdown, or a wave creature summoned,
 *   until the ritual ends (the aura gone and no wave creature left).
 *
 * Timeline: measured from the first refresh that sees the Idol Shutdown aura (up to one
 * perception interval late, as Zul'Farrak's countdown). The spawner then walks its 4 creatures
 * ~15 yd to him; SPAWN_TRAVEL_SEC covers that. wavesArrived is how many SPAWN_SEC are past;
 * wavesLeft the rest; nextArrivalSec the next SPAWN_SEC + travel from now.
 *
 * Known limits:
 *   - The ritual phase is not exposed by the instance; the schedule is the script's fixed
 *     timers, read from the aura's start, so a server that retimes the ritual drifts.
 *   - No positioning: where to stand to meet the waves is the generic modules'.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_RAZORFEN_BELNISTRASZ_H
#define MANGOS_COMMANDER_ENCOUNTER_RAZORFEN_BELNISTRASZ_H

#include "Commander/Encounters/IEncounter.h"
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
        static bool IsWaveEntry(uint32 entry);
        bool RitualOn(TeamBotAI* bot, WorldModel const& world) const;

        uint32 m_ritualStartMs = 0;     // first refresh that saw the aura, 0 = not started
        bool m_active = false;          // ritual on or a wave creature left
        uint32 m_wavesArrived = 0;
        uint32 m_wavesLeft = 0;
        float m_nextArrivalSec = -1.0f;
        bool m_startNoted = false;
    };
}

#endif
