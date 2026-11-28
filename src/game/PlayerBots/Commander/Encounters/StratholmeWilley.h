/*
 * PartyBot Commander boss module: Cannon Master Willey, Stratholme (boss doc 2.3, P3).
 *
 * Script (boss_cannon_master_willey.cpp): while Willey fights, he summons 3 Crimson Riflemen
 * every 10 s with no cap; the player shoots them with the Scarlet cannon. It is one unbroken
 * fight, so nobody sits down until he is dead (WavePace::Continuous). The budget keeps one more
 * average fight of mana on top of the enemies in sight (wavesLeft 1: the riflemen have no end).
 *
 * Match: map 329 and Willey (10997) or a Crimson Rifleman (11054) among the enemies.
 *
 * The riflemen (fodder, they stand back and shoot) are the generic focus and area tactic's; he
 * despawns them on evade. This module only sets the continuous pace and a start note.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_STRATHOLME_WILLEY_H
#define MANGOS_COMMANDER_ENCOUNTER_STRATHOLME_WILLEY_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class StratholmeWilley : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 329;
        static constexpr uint32 NPC_WILLEY = 10997;
        static constexpr uint32 NPC_RIFLEMAN = 11054;

        char const* GetId() const override { return "strat-willey"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void DescribeWaves(WaveEventFacts& out) const override;
        void OnReset() override;

    private:
        bool m_active = false;
        bool m_startNoted = false;
    };
}

#endif
