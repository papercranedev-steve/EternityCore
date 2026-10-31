/*
 * PartyBot Commander layer 2 (team assignment): the party plan.
 * Plain data written by assigners, read by behaviors. Map thread only.
 */

#ifndef MANGOS_COMMANDER_TEAM_PLAN_H
#define MANGOS_COMMANDER_TEAM_PLAN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/WorldModel.h"
#include <string>
#include <vector>

namespace Commander
{
    enum class PullPhase : uint8
    {
        None,
        WaitReady,  // waiting for health / mana / everyone nearby
        Approach,   // tank goes to the target until the fight starts
        Return,     // tank brings the enemies back to the fight spot
        Settle,     // damage dealers wait while the tank builds first threat
    };

    enum class PullOutcome : uint8
    {
        None,
        Done,       // pull phases finished (the fight itself may still be going)
        Cancelled,
    };

    // `.partybot ai clear`: pull nearby packs one after another (ClearPlanner).
    struct ClearPlan
    {
        bool active = false;
        Position3 anchor;               // leader position at start; moving away stops it
        float radius = 40.0f;
        uint32 maxMobs = 4;             // skip packs predicted to bring more
        uint32 cleared = 0;
        uint32 skippedPacks = 0;
        std::vector<ObjectGuid> skipped;    // members of packs judged too hard or unpullable
        uint32 idleSinceMs = 0;         // last time the party was in a fight
    };

    struct PullPlan
    {
        PullPhase phase = PullPhase::None;
        ObjectGuid target;
        ObjectGuid tank;
        std::vector<ObjectGuid> expected;   // AssistPredictor result, target included
        Position3 spot;                     // where the party fights (leader position at command time)
        bool returnToSpot = false;
        uint32 phaseSinceMs = 0;
    };
    // Who stops casts of one enemy. Backup acts when the primary cannot (see InterruptBehavior).
    struct InterruptDuty
    {
        ObjectGuid enemy;
        ObjectGuid primary;
        uint32 primarySpellId = 0;
        ObjectGuid backup;
        uint32 backupSpellId = 0;
        bool castingNow = false;     // enemy has an interruptible cast in progress
        uint8 category = 0;          // CastCategory of the current or last cast
    };

    struct TeamPlan
    {
        uint32 version = 0;
        std::vector<InterruptDuty> interrupts;
        PullPlan pull;
        PullOutcome lastPullOutcome = PullOutcome::None;    // set when a pull ends, read by ClearPlanner
        ClearPlan clear;
        std::vector<std::string> notes;     // messages for the leader, flushed by PartyCommander

        InterruptDuty const* FindInterruptDuty(ObjectGuid enemy) const
        {
            for (InterruptDuty const& d : interrupts)
                if (d.enemy == enemy)
                    return &d;
            return nullptr;
        }
    };
}

#endif
