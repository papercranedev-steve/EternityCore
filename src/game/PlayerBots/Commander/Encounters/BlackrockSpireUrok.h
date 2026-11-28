/*
 * PartyBot Commander boss module: Urok Doomhowl, Lower Blackrock Spire (boss doc 2.3, P3).
 *
 * Script (boss_urok.cpp, a GameObject AI): a player uses the challenge at Urok's tribute pile;
 * 3 s later one Ogre Magus and two Enforcers appear at the summoning circles, in combat with
 * everyone. Each ogre that dies is replaced at once, up to seven replacements, and the eighth
 * death spawns Urok. There is no breather between replacements, so it is one unbroken fight
 * (WavePace::Continuous): nobody sits down, the mana budget keeps for the whole fight.
 *
 * Match: map 229 and Urok (10584) or one of his ogres (Enforcer 10601, Ogre Magus 10602) among
 *   the enemies.
 *
 * The ogres are the generic focus and area tactic's. This module sets the continuous pace and a
 * start note.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_SPIRE_UROK_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_SPIRE_UROK_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class BlackrockSpireUrok : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 229;
        static constexpr uint32 NPC_UROK = 10584;
        static constexpr uint32 NPC_ENFORCER = 10601;
        static constexpr uint32 NPC_OGRE_MAGUS = 10602;

        char const* GetId() const override { return "lbrs-urok"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

    private:
        static bool IsUrokEntry(uint32 entry);

        bool m_active = false;
        bool m_startNoted = false;
    };
}

#endif
