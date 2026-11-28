/*
 * PartyBot Commander boss module: Herod, Scarlet Monastery armory (boss doc 2.1, P1).
 *
 * Script (boss_herod.cpp): his Whirlwind, Cleave and Frenzy are on his ability sheet
 * (BossSheets.cpp; the Whirlwind zone keeps melee off while the aura is up). At aggro he
 * summons 4 elite Scarlet Myrmidons at the armory door; they join only when he is taken more
 * than 32 yd from the room's centre. When he dies, 20 Scarlet Trainees (6575, 0.15 health)
 * come out of the door and run into the room.
 *
 * Match: map 189 and Herod (3975) or a summoned trainee among the enemies, or Herod gone
 *   from the enemies within TRAINEE_WAIT_MS after he was last seen at or below DEATH_PCT
 *   (Refresh then checks he is really dead; alive, i.e. he evaded: no wait).
 *
 * Rest (ForbidsRest): from his death until TRAINEE_WAIT_MS passed with no trainee fighting,
 *   and while trainees fight. The area tactic and the focus on them are the generic modules'
 *   (fodder: their health).
 * Notes: one at the start (keep him in the room); a voice line when he dies.
 *
 * Known limits:
 *   - Nothing keeps the tank from taking him out of the room (no "stay inside" for the fight
 *     spot); the note tells the player.
 *   - A death between two refreshes from above DEATH_PCT (a big crit) is not seen: no wait
 *     then, the trainees start the next fight on their own.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_SCARLET_HEROD_H
#define MANGOS_COMMANDER_ENCOUNTER_SCARLET_HEROD_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"

namespace Commander
{
    class ScarletHerod : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 189;
        static constexpr uint32 NPC_HEROD = 3975;
        static constexpr uint32 NPC_TRAINEE = 6575;

        // Last seen at or below this, then gone: he died (Refresh confirms on the live creature).
        static constexpr float DEATH_PCT = 10.0f;
        // The trainees start 1-6 s after the summon and walk ~60 yd at 2.2x walk speed.
        static constexpr uint32 TRAINEE_WAIT_MS = 25000;

        char const* GetId() const override { return "herod"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        bool ForbidsRest() const override { return m_noRest; }
        void OnReset() override;

    private:
        bool InWait(uint32 nowMs) const;

        ObjectGuid m_herod;
        float m_lastPct = 100.0f;
        uint32 m_lastSeenMs = 0;
        uint32 m_diedAtMs = 0;          // confirmed dead, 0 = not yet
        bool m_evaded = false;          // gone but alive: no wait
        bool m_noRest = false;
        bool m_startNoted = false;
    };
}

#endif
