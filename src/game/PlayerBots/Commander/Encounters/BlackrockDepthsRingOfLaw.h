/*
 * PartyBot Commander boss module: the Ring of Law, Blackrock Depths (boss doc 2.2, P2).
 *
 * Script (blackrock_depths.cpp npc_grimstoneAI): walking into the arena sets TYPE_RING_OF_LAW
 * IN_PROGRESS and brings High Justice Grimstone. He opens a gate and sends two groups of beasts
 * (one entry of RingMob for the first group, another for the second: 4 and 4, a few seconds
 * apart, the second about 20 s after the first), waits until they are dead and walks, then opens
 * the other gate for one boss at random (Gorosh, Grizzle, Eviscerator, Ok'thor, Anub'shiah,
 * Hedrum), or Theldren's band when the Banner of Provocation challenge was set. The C++ bosses
 * have ability sheets (BossSheets.cpp: Gorosh's Whirlwind, Grizzle's Ground Tremor, ...).
 *
 * Match: map 230 and a ring beast, ring boss or Theldren's band among the enemies; once active,
 *   kept on while the instance says IN_PROGRESS (Grimstone's walks between the groups).
 *
 * Waves (DescribeWaves): one fight with short walks between the groups, too short to drink
 *   (WavePace::Continuous): nobody sits down until it is over. After QUIET_MS without anyone
 *   fighting (EncounterHelpers::WaveWatch) the event is not reported: after a wipe the instance
 *   stays IN_PROGRESS once the boss was out (the script keeps him for the next try), and the
 *   party must drink before it goes back in.
 *
 * Known limits:
 *   - Grimstone's walk before the boss is not timed: a drink is never started in it.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_RING_OF_LAW_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_RING_OF_LAW_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class BlackrockDepthsRingOfLaw : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 230;
        // blackrock_depths.h: TYPE_RING_OF_LAW GetData key; ScriptedInstance.h IN_PROGRESS.
        static constexpr uint32 DATA_RING_OF_LAW = 0;
        static constexpr uint32 STATE_IN_PROGRESS = 1;

        char const* GetId() const override { return "brd-ring-of-law"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

        static constexpr uint32 QUIET_MS = 30000;

        static bool IsRingEntry(uint32 entry);

    private:
        bool m_active = false;
        bool m_startNoted = false;
        bool m_quiet = false;               // nobody fought for QUIET_MS
        EncounterHelpers::WaveWatch m_watch;
    };
}

#endif
