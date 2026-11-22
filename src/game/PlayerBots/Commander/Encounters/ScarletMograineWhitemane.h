/*
 * PartyBot Commander boss module: Scarlet Commander Mograine and High Inquisitor Whitemane,
 * Scarlet Monastery cathedral (combat scenario 7: 1.3 phase table, 3.5 problem 3).
 *
 * Match: map 189 and Mograine (3976) or Whitemane (3977) among the enemies.
 *
 * Stage: instance data TYPE_MOGRAINE_AND_WHITE_EVENT, read in Refresh (map thread).
 *   Values (instance_scarlet_monastery.cpp, enum in the .cpp, not in scarlet_monastery.h;
 *   the scripts library is not on the game include path, so they are repeated here):
 *   0 NOT_STARTED, 1 IN_PROGRESS, 2 DIED_ONCE (Mograine feigning death, Whitemane coming),
 *   3 REVIVED (both up), 4 DONE.
 *   Without instance data (scripts not loaded) the stage is inferred from facts: Mograine
 *   feigned = DIED_ONCE (not acting; alive and feigning death / not selectable / not a valid
 *   target; or off the threat lists while Whitemane is engaged); back on a threat list and
 *   acting after that = REVIVED.
 *
 * Match also holds without either boss on a threat list (feign death gap before Whitemane
 *   arrives) for BOSS_GONE_GRACE_MS after a boss was last in the fight, unless the party has
 *   been out of combat for PARTY_OUT_OF_COMBAT_MS or both bosses are dead.
 *
 * Plan (AdjustPlan): focus Whitemane in DIED_ONCE and REVIVED (she heals, mind controls, and
 *   only her health moves phase 1 forward). In DIED_ONCE before she arrives the focus is
 *   cleared (Mograine cannot be attacked).
 *
 * Proposals:
 *   Tank, Urgent, taunt Mograine within REVIVE_TAUNT_WINDOW_MS of REVIVED while he is acting
 *     and hitting someone else: healer threat piled up on him during the feign death
 *     (problem 3). benefit = Value::HitLoss of Mograine on his victim for REVIVE_LOOSE_SEC,
 *     cost = one global cooldown of the tank.
 *   Tank, Assigned, attack Whitemane in DIED_ONCE when the tank is not on her: first threat
 *     before heal threat pulls her. benefit = her HitLoss on a non-tank victim over
 *     ThreatBehavior::TANK_RECLAIM_SEC + FIRST_THREAT_POINTS, cost = a target switch.
 *   Healer, Assigned, heal the tank in DIED_ONCE while 50% < Whitemane <= 55% (the 10 s Deep
 *     Sleep comes at 50%, nobody heals during it, Mograine wakes first). benefit =
 *     SLEEP_WEIGHT x health restored (points), cost = the healer's contribution during the cast.
 *
 * Known limits: see the .cpp file comment at the top.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_SCARLET_MOGRAINE_WHITEMANE_H
#define MANGOS_COMMANDER_ENCOUNTER_SCARLET_MOGRAINE_WHITEMANE_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "ObjectGuid.h"
#include <string>

namespace Commander
{
    struct EnemyFacts;
    struct PartyMemberFacts;

    class ScarletMograineWhitemane : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 189;
        static constexpr uint32 NPC_MOGRAINE = 3976;
        static constexpr uint32 NPC_WHITEMANE = 3977;

        // scarlet_monastery.h: TYPE_MOGRAINE_AND_WHITE_EVENT = 1.
        static constexpr uint32 TYPE_MOGRAINE_AND_WHITE_EVENT = 1;
        // instance_scarlet_monastery.cpp: STAGE_MOGRAINE_*.
        static constexpr uint32 STAGE_NOT_STARTED = 0;
        static constexpr uint32 STAGE_IN_PROGRESS = 1;
        static constexpr uint32 STAGE_DIED_ONCE = 2;
        static constexpr uint32 STAGE_REVIVED = 3;
        static constexpr uint32 STAGE_DONE = 4;

        // REVIVED is set 4 s after the resurrection hits (generic_scripts 9232); the party sleeps
        // until ~3 s after that. 10 s covers waking up and one taunt attempt.
        static constexpr uint32 REVIVE_TAUNT_WINDOW_MS = 10000;
        static constexpr float REVIVE_LOOSE_SEC = 5.0f;         // hits on his victim if nobody taunts

        static constexpr float FIRST_THREAT_POINTS = 2.0f;      // tank on Whitemane before her first heal threat

        // Whitemane's Deep Sleep threshold (creature_ai_events 397702) and the top-up band above it.
        static constexpr float SLEEP_PCT = 50.0f;
        static constexpr float PRE_SLEEP_PCT = 55.0f;
        static constexpr float TANK_TOPUP_PCT = 95.0f;          // no top-up above this
        static constexpr float OTHER_CRITICAL_PCT = 40.0f;      // someone else this low: rotation decides
        // Health restored before a 10 s window without heals counts this many times.
        static constexpr float SLEEP_WEIGHT = 2.0f;
        static constexpr float HEAL_CAST_SEC = 2.5f;            // typical direct heal cast

        // Feign death takes Mograine off the threat lists before Whitemane is on them: the module
        // stays active this long after a boss was last seen in the fight (threat list, or live:
        // Mograine feigning, Whitemane in combat) ...
        static constexpr uint32 BOSS_GONE_GRACE_MS = 20000;
        // ... unless the party has been out of combat this long.
        static constexpr uint32 PARTY_OUT_OF_COMBAT_MS = 5000;

        char const* GetId() const override { return "mograine-whitemane"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void AdjustPlan(WorldModel const& world, TeamPlan& plan) override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        void OnReset() override;

        uint32 GetStage() const { return m_stage; }

    private:
        void ProposeTank(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeHealer(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void SetStage(uint32 stage, uint32 nowMs);

        uint32 m_stage = STAGE_NOT_STARTED;
        bool m_stageFromInstance = false;       // false: inferred from facts
        bool m_sawFeignDeath = false;           // inference: Mograine was seen not acting
        uint32 m_stageSinceMs = 0;
        uint32 m_revivedAtMs = 0;               // 0 = not revived this fight
        uint32 m_bossEngagedMs = 0;             // last refresh a boss was in the fight, 0 = over / never
        uint32 m_partyCombatMs = 0;             // last refresh a party member was in combat

        ObjectGuid m_mograine;
        ObjectGuid m_whitemane;
        ObjectGuid m_tank;

        std::vector<std::string> m_pendingNotes;

        std::vector<VoiceEvent> m_pendingVoice;      // flushed into plan.voice by AdjustPlan (the party voice)
    };
}

#endif
