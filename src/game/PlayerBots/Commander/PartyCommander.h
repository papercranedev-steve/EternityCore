/*
 * PartyBot Commander: one instance per (group, map instance).
 * Keyed by map so that it is only ever touched from that map's update thread.
 */

#ifndef MANGOS_COMMANDER_PARTY_COMMANDER_H
#define MANGOS_COMMANDER_PARTY_COMMANDER_H

#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ThreatHistory.h"
#include "Commander/Perception/CombatRates.h"
#include "Commander/Registry.h"
#include "Commander/Encounters/IEncounter.h"

class PartyBotAI;

namespace Commander
{
    class PartyCommander
    {
    public:
        PartyCommander(uint32 groupId, uint32 mapId, uint32 instanceId);

        // Every bot tick, in and out of combat. Refreshes facts at most once per perceptionMs,
        // so several bots of the same party share one refresh.
        void Update(PartyBotAI* bot, uint32 nowMs, uint32 perceptionMs, AssignerList const& assigners);

        // Combat tick of one bot: behaviors + arbiter. True = action taken, skip original AI.
        bool RunCombat(PartyBotAI* bot, BehaviorList const& behaviors);

        WorldModel const& GetWorld() const { return m_world; }
        TeamPlan const& GetPlan() const { return m_plan; }
        // GM commands only (world thread while maps are idle).
        TeamPlan& GetPlanMutable() { return m_plan; }

        // An out-of-combat plan (pull) needs the Commander hook to run before the original AI.
        // WaitReady is excluded: bots keep eating and drinking while the party gets ready.
        // While a pull or clear is running and the party is already fighting (player attacked
        // first, a patrol walked in), a bot not yet on any threat list must stand up and help
        // instead of sitting there drinking. Outside pull / clear the original AI is unchanged.
        // "Fighting" = an enemy with one of us on its threat list: an ally's fight nearby (a
        // guard, an escort) does not stop the party from drinking.
        // The active boss module may ask for the same (IEncounter::WantsOutOfCombatControl):
        // Zul'Farrak pyramid waves, where the next group arrives while bots are out of combat.
        bool WantsOutOfCombatControl() const
        {
            PullPhase const phase = m_plan.pull.phase;
            bool const pulling = phase != PullPhase::None && phase != PullPhase::WaitReady;
            bool const planRunning = phase != PullPhase::None || m_plan.clear.active;
            // A retreating bot that already dropped combat must keep running to the rally point,
            // not sit down to drink or follow the leader back into the enemies.
            return pulling || m_plan.retreat.active || (planRunning && m_world.HasPartyEnemy()) ||
                   (m_activeEncounter && m_activeEncounter->WantsOutOfCombatControl());
        }
        uint32 GetGroupId() const { return m_groupId; }
        uint32 GetMapId() const { return m_mapId; }
        uint32 GetInstanceId() const { return m_instanceId; }

        // Boss module matched on the last refresh, null if none. Map thread only
        // (EncounterBehavior, debug commands).
        IEncounter const* GetActiveEncounter() const { return m_activeEncounter; }

    private:
        void Refresh(PartyBotAI* bot, uint32 nowMs);
        void RunAssigners(AssignerList const& assigners);
        // Picks the matching boss module (OnReset on the one that stops matching), then its
        // Refresh and AddFacts. Before the assigners.
        void UpdateEncounter(PartyBotAI* bot);
        void FlushNotes(PartyBotAI* bot);
        // After the assigners: when the retreat ended since the last refresh, releases the
        // movement hold of every bot of the party on this map.
        void ReleaseMovementAfterRetreat(PartyBotAI* bot);

        uint32 m_groupId;
        uint32 m_mapId;
        uint32 m_instanceId;
        WorldModel m_world;
        TeamPlan m_plan;
        ThreatHistory m_threatHistory;
        CombatRates m_combatRates;
        bool m_wasRetreating = false;   // m_plan.retreat.active on the previous refresh

        // Own instances (per-party state), only the modules listed for this map.
        EncounterList m_encounters;
        IEncounter* m_activeEncounter = nullptr;
    };
}

#endif
