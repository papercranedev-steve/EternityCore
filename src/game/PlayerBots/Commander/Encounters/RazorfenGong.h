/*
 * PartyBot Commander boss module: the Tuten'kash gong, Razorfen Downs (boss doc 2.3, P3).
 *
 * Script (instance_razorfen_downs.cpp): a player rings the gong (GO 148917), which locks and
 * summons 8 Tomb Fiends; when all are dead the gong unlocks; a second ring summons 4 Tomb
 * Reavers; a third ring summons Tuten'kash. The instance keeps a stage counter DATA_GONG_WAVES
 * (a click and each Fiend / Reaver death each add 1): 1 = Fiends fighting, 9 = Fiends cleared
 * (gong open), 10 = Reavers fighting, 14 = Reavers cleared, 15 = Tuten'kash. Each wave needs a
 * player's ring after the last is dead, so the party drinks to full in between.
 *
 * Player-paced (WavePace::Player): the mana budget keeps nothing for future waves, WaveRest
 * lets bots drink to full in the gaps, and PartyVoice says "drinking" / "ready".
 *
 * Match: map 129 and the gong stage is 1..14 (Tuten'kash, stage 15, is a plain boss fight),
 *   or a Fiend / Reaver among the enemies.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_RAZORFEN_GONG_H
#define MANGOS_COMMANDER_ENCOUNTER_RAZORFEN_GONG_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class RazorfenGong : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 129;
        // razorfen_downs.h: DATA_GONG_WAVES GetData key (eData index 1).
        static constexpr uint32 DATA_GONG_WAVES = 1;
        static constexpr uint32 NPC_TOMB_FIEND = 7349;
        static constexpr uint32 NPC_TOMB_REAVER = 7351;

        char const* GetId() const override { return "rfd-gong"; }
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
