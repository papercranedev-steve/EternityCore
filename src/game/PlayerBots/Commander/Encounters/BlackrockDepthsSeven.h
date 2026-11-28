/*
 * PartyBot Commander boss module: the Tomb of the Seven, Blackrock Depths (boss doc 2.2, P2).
 *
 * Script (boss_tomb_of_seven.cpp boss_doomrelAI): a player talks to Doom'rel, which sets
 * TYPE_TOMB_OF_SEVEN IN_PROGRESS. From then on Doom'rel calls one dwarf every 30 s, whether the
 * last one is dead or not: Anger'rel, Seeth'rel, Dope'rel, Gloom'rel, Vile'rel, Hate'rel, then
 * himself (summons voidwalkers at 50%). If the dwarf called 25 s earlier has no target the event
 * fails (FAIL, all reset). The six are EventAI with spell lists (Vile'rel heals: the generic kill
 * order takes healers first); Doom'rel has an ability sheet (BossSheets.cpp).
 *
 * Match: map 230 and one of the seven among the enemies; once active, kept on while the
 *   instance says IN_PROGRESS (the gaps between dwarves count too).
 *
 * Waves (DescribeWaves): seven on a 30 s clock (WavePace::Timer). The clock runs from the first
 *   time each dwarf is seen as an enemy (EnemyFacts::firstSeenMs); the next comes 30 s after the
 *   last one seen. WaveRest decides whether a drink fits in a gap.
 *
 * Known limits:
 *   - A dwarf called while the party fights out of sight of it is counted when it is seen.
 *   - The 30 s clock starts at the gossip, which is not seen: the first dwarf starts it.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_SEVEN_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_SEVEN_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <vector>

namespace Commander
{
    class BlackrockDepthsSeven : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 230;
        // blackrock_depths.h: TYPE_TOMB_OF_SEVEN GetData key; ScriptedInstance.h IN_PROGRESS.
        static constexpr uint32 DATA_TOMB_OF_SEVEN = 3;
        static constexpr uint32 STATE_IN_PROGRESS = 1;
        static constexpr uint32 DWARF_COUNT = 7;
        static constexpr float CALL_INTERVAL_SEC = 30.0f;

        char const* GetId() const override { return "brd-seven"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

        // Hate'rel 9034 .. Dope'rel 9040, Doom'rel 9039 included.
        static bool IsDwarf(uint32 entry) { return entry >= 9034 && entry <= 9040; }

    private:
        bool m_active = false;
        bool m_startNoted = false;
        std::vector<ObjectGuid> m_seen;     // dwarves seen as enemies this event
        uint32 m_lastCallMs = 0;            // WorldTimer ms the last of them was first seen
        uint32 m_nowMs = 0;
    };
}

#endif
