/*
 * PartyBot Commander: one instance per (team, map instance): a team of any size (Commander/Team.h).
 * Keyed by map so that it is only ever touched from that map's update thread.
 */

#ifndef MANGOS_COMMANDER_PARTY_COMMANDER_H
#define MANGOS_COMMANDER_PARTY_COMMANDER_H

#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/ThreatHistory.h"
#include "Commander/Perception/Forecast/CombatRates.h"
#include "Commander/Perception/Spatial/MeleeRoutes.h"
#include "Commander/Perception/Spatial/PositionFieldBuilder.h"
#include "Commander/Perception/Forecast/FleePredictor.h"
#include "Commander/Perception/Spatial/LeaderRun.h"
#include "Commander/Registry.h"
#include "Commander/ThreatFeed.h"
#include "Commander/Encounters/IEncounter.h"
#include "Commander/Trace/Trace.h"
#include <deque>
#include <string>

class TeamBotAI;

namespace Commander
{
    class PartyCommander
    {
    public:
        PartyCommander(uint64 teamKey, uint32 mapId, uint32 instanceId);

        // Every bot tick, in and out of combat. Refreshes facts at most once per perceptionMs,
        // so several bots of the same party share one refresh.
        void Update(TeamBotAI* bot, uint32 nowMs, uint32 perceptionMs, AssignerList const& assigners,
                    BehaviorList const& behaviors);

        // Combat tick of one bot: behaviors + arbiter. True = action taken, skip original AI.
        bool RunCombat(TeamBotAI* bot, BehaviorList const& behaviors);

        WorldModel const& GetWorld() const { return m_world; }
        TeamPlan const& GetPlan() const { return m_plan; }
        // GM commands only (world thread while maps are idle).
        TeamPlan& GetPlanMutable() { return m_plan; }

        // An out-of-combat plan (pull) needs the Commander hook to run before the original AI.
        // WaitReady too since 2026-11-07: healers and ranged walk to their places for the planned
        // fight first, then eat and drink there (RestBehavior; the original eat and drink is gone).
        // While the party is already fighting (player attacked first, a patrol walked in), a bot
        // not yet on any threat list helps, or keeps drinking while that pays (RestBehavior).
        // "Fighting" = an enemy with one of us on its threat list: an ally's fight nearby (a
        // guard, an escort) does not stop the party from drinking.
        // The active boss module forbidding rest (TeamPlan::noRest, IEncounter::ForbidsRest):
        // Zul'Farrak pyramid waves, where the next group arrives while bots are out of combat.
        bool WantsOutOfCombatControl() const
        {
            PullPhase const phase = m_plan.pull.phase;
            bool const pulling = phase != PullPhase::None;
            // A retreating bot that already dropped combat must keep running to the rally point,
            // not sit down to drink or follow the leader back into the enemies.
            // Since 2026-11-07 the party fighting, or one of us attacking something not fighting
            // yet (WorldModel::targeted), is enough: the original target selection is gone, the
            // first target of a bot comes from TargetAssigner through the Commander hook.
            // The push mode's guide walking a leg (PushBehavior moves it out of combat).
            // A team of bots going for its next fight (HuntAssigner).
            return pulling || m_plan.retreat.active || m_world.HasPartyEnemy() || !m_world.targeted.empty() ||
                   m_plan.noRest || m_plan.push.walking || m_plan.hunt.Active();
        }
        uint64 GetTeamKey() const { return m_teamKey; }
        uint32 GetMapId() const { return m_mapId; }
        uint32 GetInstanceId() const { return m_instanceId; }

        // Boss module matched on the last refresh, null if none. Map thread only
        // (EncounterBehavior, debug commands).
        IEncounter const* GetActiveEncounter() const { return m_activeEncounter; }

        // Notes sent to the leader (TeamPlan::notes), the last NOTE_LOG_SIZE, oldest first.
        // Kept for the dev console (u:notes, combat recorder): a headless leader drops the chat
        // message and the plan clears its notes once sent.
        struct NoteRecord
        {
            uint32 timeMs = 0;      // server ms (WorldTimer::getMSTime) when sent
            std::string text;
        };
        static constexpr size_t NOTE_LOG_SIZE = 50;
        std::deque<NoteRecord> const& GetNoteLog() const { return m_noteLog; }

        // Calculation trace of this party (Trace/Trace.h): installed (Trace::Use) around the
        // refresh, the bots' combat ticks and the pull planning; armed by the dev console
        // (u:trace) in the world thread while maps are idle.
        Trace::Sink& GetTrace() { return m_trace; }

    private:
        void Refresh(TeamBotAI* bot, uint32 nowMs);
        void RunAssigners(AssignerList const& assigners);
        // Picks the matching boss module (OnReset on the one that stops matching), then its
        // Refresh and AddFacts. Before the assigners.
        void UpdateEncounter(TeamBotAI* bot);
        // TeamPlan::notes: said in party chat by the main tank bot (BotSpeech::Report), or system
        // messages to the leader with speech off; then into the note log.
        void FlushNotes(TeamBotAI* bot);
        // After the assigners: when the retreat ended since the last refresh, releases the
        // movement hold of every bot of the party on this map.
        void ReleaseMovementAfterRetreat(TeamBotAI* bot);
        // After the assigners: gives the bots of this map the roles of TeamPlan::roles
        // (RoleAssigner), out of combat only, and tells the leader.
        void ApplyRoles(TeamBotAI* bot);
        // After the assigners, before ApplyRoles: gives the bots of this map their
        // TeamPlan::emergencyRoles (CrisisAssigner, a stand-in tank, a spare healer), and the
        // old role back to a bot no longer listed once the party is out of the fight.
        void ApplyEmergencyRoles(TeamBotAI* bot);
        // Last step of a refresh, in combat: the other bots of the party on this map that a
        // behavior has urgent work for (IBehavior::WantsTickNow) run their next update in the
        // next frame (TeamBotAI::WakeUp) instead of after the rest of their bot interval.
        void WakeBots(TeamBotAI* bot, BehaviorList const& behaviors);

        uint64 m_teamKey;
        uint32 m_mapId;
        uint32 m_instanceId;
        WorldModel m_world;
        TeamPlan m_plan;
        ThreatHistory m_threatHistory;
        CombatRates m_combatRates;
        MeleeRoutes m_meleeRoutes;
        PositionFieldBuilder m_positionField;
        FleePredictor m_fleePredictor;
        LeaderRun m_leaderRun;
        ThreatFeed m_threatFeed;        // addon threat meter of subscribed players
        bool m_wasRetreating = false;   // m_plan.retreat.active on the previous refresh

        // Own instances (per-party state), only the modules listed for this map.
        EncounterList m_encounters;
        IEncounter* m_activeEncounter = nullptr;

        std::deque<NoteRecord> m_noteLog;
        Trace::Sink m_trace;
    };
}

#endif
