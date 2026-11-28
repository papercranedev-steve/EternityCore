/*
 * PartyBot Commander boss module: Baron Rivendare, Stratholme (boss doc 2.4).
 *
 * Script: EventAI (creature_ai_scripts 1044001): his aggro sets TYPE_BARON IN_PROGRESS, and
 * instance_stratholme.cpp shuts the gate GO_ZIGGURAT5 (175796) at (4032.69, -3364.54), 28 yd
 * from him; DONE (his death) or FAIL (evade) opens it again. His Unholy Aura (17467) hits
 * everyone within 30 yd (17466's radius) every 2 s, so a step through the gate starts him and
 * shuts it (rivendare-20261128-190203: only the tank was in; -191005: the player stepped in
 * and back, and he was fought through the shut gate, before closed doors stopped swings).
 *
 * Before the pull (BeforePull): a sealed room (BossPrep::seal, Perception/Spatial/SealedRoom.h)
 * from the gate, him and the aura's radius. The generic parts do the rest: the bots follow the
 * leader close near the gate (HumanFollow CLOSE_*), the clear asks about him only with
 * everyone at the gate (ClearPlanner), the pull is a walk-in fought in the room, the others
 * walk in behind the tank (PullBehavior), and the party says the gate shuts
 * (voice.boss.sealed.gather).
 *
 * In the fight (Refresh): the gate shut with a member outside: said once
 * (voice.boss.sealed.locked_out). Nothing else: his skeletons (Raise Dead) are the generic
 * modules'.
 *
 * Known limits: a member left outside only waits at the gate (no way in until the fight is
 * over); a healer still outside in the 0-2 s before the first pulse may take its place outside.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_STRATHOLME_RIVENDARE_H
#define MANGOS_COMMANDER_ENCOUNTER_STRATHOLME_RIVENDARE_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Perception/Spatial/SealedRoom.h"
#include "ObjectGuid.h"

namespace Commander
{
    class StratholmeRivendare : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 329;
        static constexpr uint32 NPC_RIVENDARE = 10440;
        static constexpr uint32 GO_GATE = 175796;
        static constexpr float GATE_X = 4032.69f;
        static constexpr float GATE_Y = -3364.54f;
        static constexpr float GATE_Z = 115.054f;
        // His spawn point (creature 10440): the room's side, fixed (he walks in the fight).
        static constexpr float HOME_X = 4035.83f;
        static constexpr float HOME_Y = -3336.31f;
        static constexpr float HOME_Z = 115.144f;
        static constexpr uint32 SPELL_UNHOLY_AURA_PULSE = 17466;
        static constexpr float AURA_FALLBACK_YD = 30.0f;
        static constexpr float GATE_SEARCH_YD = 120.0f;
        static constexpr uint32 GATE_SEARCH_RETRY_MS = 5000;
        // A member this near the gate on the outside counts as shut out (not one that ran off).
        static constexpr float LOCKED_OUT_YD = 40.0f;

        char const* GetId() const override { return "rivendare"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void BeforePull(WorldModel const& world, std::vector<BossPrep>& out) const override;
        void OnReset() override;

    private:
        static SealedRoom Room();

        ObjectGuid m_gate;
        uint32 m_lastSearchMs = 0;
        bool m_lockedOutSaid = false;
    };
}

#endif
