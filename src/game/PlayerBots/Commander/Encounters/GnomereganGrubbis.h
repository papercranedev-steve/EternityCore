/*
 * PartyBot Commander boss module: the Grubbis event (Blastmaster Emi Shortfuse), Gnomeregan
 * (boss doc 2.3, P3).
 *
 * Script (gnomeregan.cpp npc_blastmaster_emi_shortfuseAI): a player starts the event by gossip;
 * Emi walks a path setting charges while Caverndeep Ambushers pour out of two cave-ins, ending
 * with Grubbis and Chomper. Her phase timer only ticks while she is out of combat, so the event
 * is effectively one rolling fight with short lulls; if she dies it fails. The lulls are too
 * short and uncertain to drink in, so nobody sits down while the event runs
 * (WavePace::Continuous).
 *
 * Match: map 90 and a summoned Caverndeep Ambusher / Burrower or Grubbis among the enemies
 *   (Matches cannot read the instance); once active, kept on while GetData(TYPE_GRUBBIS) ==
 *   IN_PROGRESS, so the lulls between packs count too. Emi dying sets FAIL and ends it.
 *
 * Emi is an escort (npc_escortAI), so the generic protect (boss doc G3) already heals her and
 * kills her attackers first. This module sets the continuous pace and a start note.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_GNOMEREGAN_GRUBBIS_H
#define MANGOS_COMMANDER_ENCOUNTER_GNOMEREGAN_GRUBBIS_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class GnomereganGrubbis : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 90;
        // gnomeregan.h: TYPE_GRUBBIS GetData key (index 0), EncounterState IN_PROGRESS.
        static constexpr uint32 DATA_GRUBBIS = 0;
        static constexpr uint32 STATE_IN_PROGRESS = 1;      // ScriptedInstance.h EncounterState (2 is FAIL)
        static constexpr uint32 NPC_AMBUSHER = 6207;
        static constexpr uint32 NPC_BURROWER = 6206;
        static constexpr uint32 NPC_GRUBBIS = 7361;

        char const* GetId() const override { return "gnomer-grubbis"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

    private:
        static bool IsEventEntry(EnemyFacts const& e);

        bool m_active = false;
        bool m_startNoted = false;
    };
}

#endif
