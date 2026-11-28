/*
 * PartyBot Commander boss module: the Fires of Aku'mai, Blackfathom Deeps (boss doc 2.3, P3).
 *
 * Script (instance_blackfathom_deeps.cpp): after Twilight Lord Kelris is dead, a player lights
 * each of the four braziers; lighting one spawns a wave 3 s later (4 Aku'mai Snapjaw, then
 * 2 Aku'mai Servant, then 4 Murkshallow Snapclaw, then 8 Murkshallow Softshell). The next wave
 * comes only when a player lights the next brazier, so the party drinks to full in between.
 *
 * This module only tells the party it is a player-paced wave event (WavePace::Player): the
 * mana budget keeps nothing for future waves, WaveRest lets bots drink to full between them,
 * and PartyVoice says "drinking" / "ready" so the player knows when to light the next fire.
 *
 * Match: map 48 and the shrine event in progress (GetData(TYPE_SHRINE) == IN_PROGRESS), or a
 *   wave creature among the enemies.
 *
 * Known limits:
 *   - The instance does not expose how many fires are lit; wavesArrived / wavesLeft are not
 *     known, so only the pace is reported (that is all a player-paced event needs for the
 *     budget and the rest rule).
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKFATHOM_FIRES_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKFATHOM_FIRES_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class BlackfathomFires : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 48;
        // blackfathom_deeps.h: TYPE_SHRINE GetData key, EncounterState IN_PROGRESS.
        static constexpr uint32 DATA_SHRINE = 11;
        static constexpr uint32 STATE_IN_PROGRESS = 1;      // ScriptedInstance.h EncounterState (2 is FAIL)
        // Wave creatures (blackfathom_deeps.h).
        static constexpr uint32 NPC_SNAPJAW = 4825;
        static constexpr uint32 NPC_SERVANT = 4978;
        static constexpr uint32 NPC_SNAPCLAW = 4815;
        static constexpr uint32 NPC_SOFTSHELL = 4977;

        char const* GetId() const override { return "bfd-fires"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

    private:
        static bool IsWaveEntry(uint32 entry);

        bool m_active = false;
        bool m_startNoted = false;
    };
}

#endif
