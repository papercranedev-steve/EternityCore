/*
 * PartyBot Commander boss module: the Disciple of Naralex's ritual, Wailing Caverns
 * (boss doc 2.3, P1).
 *
 * Script (wailing_caverns.cpp npc_disciple_of_naralexAI): the escorted disciple reaches the
 * final chamber, channels Naralex's Awakening (6271, a self stun) and summons three waves that
 * run at him: 3 Deviate Moccasin (~12 s), 7 Nightmare Ectoplasm (~52 s), then Mutanus the
 * Devourer (~102 s). The waves are on the script's clock, not kill-gated; if the disciple dies
 * the event fails.
 *
 * He is an escort (npc_escortAI escorting, with no player guid: the generic protect by G3 still
 * sees him channelling and heals him, kills his attackers first). This module adds the wave
 * schedule (WorldModel::waves) for the mana budget and gap drinking, and a start note.
 *
 * Match: map 43 and the disciple (3678) channelling Awakening, or a wave creature summoned,
 *   until the ritual ends.
 *
 * Known limits:
 *   - Mutanus (an elite boss, not an add) is the last "wave": the budget treats him like a wave
 *     of mana to keep for, which is what the party wants (do not run dry before him).
 *   - The schedule is the script's fixed timers from the aura's start; a retimed server drifts.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_WAILING_CAVERNS_NARALEX_H
#define MANGOS_COMMANDER_ENCOUNTER_WAILING_CAVERNS_NARALEX_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"

namespace Commander
{
    class WailingCavernsNaralex : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 43;
        static constexpr uint32 NPC_DISCIPLE = 3678;
        static constexpr uint32 SPELL_AWAKENING = 6271;
        // Wave creatures (wailing_caverns.cpp).
        static constexpr uint32 NPC_MOCCASIN = 5762;
        static constexpr uint32 NPC_ECTOPLASM = 5763;
        static constexpr uint32 NPC_MUTANUS = 3654;

        char const* GetId() const override { return "wc-naralex"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

    private:
        static bool IsWaveEntry(uint32 entry);
        bool RitualOn(TeamBotAI* bot, WorldModel const& world) const;

        uint32 m_ritualStartMs = 0;
        bool m_active = false;
        uint32 m_wavesArrived = 0;
        uint32 m_wavesLeft = 0;
        float m_nextArrivalSec = -1.0f;
        bool m_startNoted = false;
    };
}

#endif
