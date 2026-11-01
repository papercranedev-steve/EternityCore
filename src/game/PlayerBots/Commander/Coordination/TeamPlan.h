/*
 * PartyBot Commander layer 2 (team assignment): the party plan.
 * Plain data written by assigners, read by behaviors. Map thread only.
 * Exception: PullPlan::rangedShot (mutable) is kept by the tank's PullBehavior.
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
        // Real failure: tank dead or gone, not ready in time, tank never reached the target.
        Cancelled,
        // Another fight started before the pull did (player attacked, a patrol walked in).
        // `.partybot ai clear` waits for that fight to end and goes on.
        Interrupted,
    };

    // `.partybot ai clear`: pull nearby packs one after another (ClearPlanner).
    struct ClearPlan
    {
        bool active = false;
        Position3 anchor;               // leader position at start, moved when the leader settles elsewhere
        float radius = 40.0f;
        uint32 maxMobs = 4;             // skip packs predicted to bring more
        uint32 cleared = 0;
        uint32 skippedPacks = 0;
        std::vector<ObjectGuid> skipped;    // members of packs judged too hard or unpullable
        uint32 idleSinceMs = 0;         // last time the party was in a fight

        // Leader walked away from the anchor (looting): paused, resumes where the leader stops.
        bool paused = false;
        Position3 leaderPos;            // last leader position that counted as a move
        uint32 leaderStillSinceMs = 0;  // when the leader stopped moving

        // Pack of an interrupted pull: counted as cleared if it is dead once that fight ends.
        std::vector<ObjectGuid> interruptedPack;

        // Deaths: wait for a resurrection after the fight, stop on the second death.
        uint32 deathEvents = 0;
        uint32 reviveWaitSinceMs = 0;   // 0 = nobody dead
        // Dead when the clear started: their death is not a death event of this clear.
        // Removed once alive again, so a later death counts.
        std::vector<ObjectGuid> deadAtStart;

        std::vector<ObjectGuid> tappedNoted;    // first member of tapped packs already reported
        // First member of packs with only stealthed hostile members, already reported. Such
        // packs are not skipped: they are looked at again every round (they may unstealth).
        std::vector<ObjectGuid> stealthNoted;
    };

    // Display name of a party member, captured when the pull is planned (the pure assigner
    // has no live objects to read names from).
    struct PullMemberName
    {
        ObjectGuid guid;
        std::string name;
    };

    // Ranged pull shot steps of one Approach (PullBehavior::NextShotStep).
    struct PullShotState
    {
        uint32 approachSinceMs = 0;     // PullPlan::phaseSinceMs of its Approach: a new Approach starts over
        uint32 shotAtMs = 0;            // first time in position, or the end of the retry walk; 0 = none yet
        bool retried = false;           // stepped RANGED_RETRY_STEP_YD closer once
        bool walkIn = false;            // gave up on the shot: walk-in pull
    };

    // Line of sight pull (PullPlanner::SearchLosBreak, PullAssigner Settle).
    enum class PullLosBreak : uint8
    {
        None,       // not searched yet (no caster standing back seen in Settle so far)
        NoPoint,    // searched: no usable point, fight where we are
        Active,     // the tank stands at losPoint until the casters come or the timeout
        Ended,
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

        // WaitReady bookkeeping.
        float healerManaPct = 80.0f;        // healer mana needed (PullPlanner::READY_MANA_PCT by default)
        uint32 botWaitMs = 0;               // time bots were not ready; only this counts toward the timeout
        uint32 lastReadyCheckMs = 0;
        bool readyNoteSent = false;         // "still waiting for ..." is sent once
        bool readyAnnounced = false;        // "everyone ready" is sent once
        std::vector<PullMemberName> names;

        // Predict again and look at patrols when the tank is about to go
        // (PullPlanner::RecheckBeforeApproach, run from ClearPlanner::Step on the map thread).
        // Set for every pull except `.partybot ai pull now`.
        bool recheckOnApproach = false;
        bool rechecked = false;
        uint32 recheckWaitSinceMs = 0;      // clear: prediction over the limit since then, 0 = not waiting
        // Last full recheck. While held (patrol, over the limit) the pull comes back to
        // Approach on every refresh; the next full recheck waits RECHECK_PATROL_WAIT_MS /
        // RECHECK_HOLD_MS after this.
        uint32 lastRecheckMs = 0;

        // Patrol expected near the target or the fight spot soon: held in WaitReady.
        uint32 patrolWaitSinceMs = 0;       // 0 = not waiting for a patrol
        ObjectGuid patrolWaitingFor;        // the patrol last announced (a new one is announced again)

        // Members of `expected` that join because the tank stands in their aggro range
        // (AssistPredictor::JoinReason::AggroRange), for the report.
        std::vector<ObjectGuid> aggroRangeAdds;

        // Ranged pull: the tank opens with this spell from up to rangedPullRange yards instead of
        // walking to the target. 0 = walk in and attack. Cleared by PullAssigner when the
        // ranged pull did not start the fight in time (tank walks in instead).
        uint32 rangedPullSpell = 0;
        float rangedPullRange = 0.0f;
        // Written by the tank's PullBehavior::Propose, the one exception to "behaviors only
        // read the plan": only the behavior sees when the tank stands in range with line of
        // sight and whether it is still on a held walk (live checks, every bot tick, not in
        // the facts). The plan belongs to one PartyCommander, which only its map thread
        // touches, so no lock is needed. Reset with the plan (new pull) and by a new Approach.
        mutable PullShotState rangedShot;

        // Line of sight pull. A pulled caster standing back casting at the tank (melee off, or
        // a main ranged spell and not moving) more than PullPlanner::LOS_CASTER_MIN_DISTANCE
        // away: during Settle the tank moves to a nearby point the caster cannot see. Its main
        // ranged spell then fails (line of sight) and the creature AI turns chase back on
        // (CreatureAI::DoSpellsListCasts), so it walks to the tank. Searched once per pull.
        PullLosBreak losBreak = PullLosBreak::None;
        Position3 losPoint;
        std::vector<ObjectGuid> losCasters;  // casters the point hides from
        uint32 losSinceMs = 0;              // Active since then (PullAssigner::LOS_BREAK_TIMEOUT_MS)
    };
    enum class RetreatOutcome : uint8
    {
        None,
        Done,       // every enemy gave up and every living member reached the point
        TimedOut,   // RetreatAssigner::TIMEOUT_MS passed first
        Stopped,    // `.partybot ai retreat stop`, or the module was switched off
    };

    // `.partybot ai retreat` (RetreatPlanner). Only the player's command starts it: the player
    // is not controlled by us, bots running on their own would leave the player alone
    // (combat scenario 11, T+18s).
    struct RetreatPlan
    {
        bool active = false;
        Position3 point;                    // rally point, out of every enemy's threat area
        uint32 startedMs = 0;
        uint32 enemiesAtStart = 0;
        RetreatOutcome lastOutcome = RetreatOutcome::None;
        std::vector<PullMemberName> names;  // for notes written by the pure assigner

        // "Consider retreating" hint (RetreatAssigner::Advise): at most once per fight.
        bool adviceSent = false;
        uint32 dangerSinceMs = 0;           // 0 = not in danger now
    };

    // Fight-time patrol response (PatrolAssigner): an idle patrol walks into the fight before
    // it ends and moving the fight is worth it. The tank walks to the first candidate that
    // passes the live ground / idle creature / path checks (PatrolBehavior); the enemies and
    // the party follow the tank.
    struct PatrolMovePlan
    {
        bool active = false;
        ObjectGuid patrol;
        ObjectGuid tank;
        std::vector<Position3> spots;       // best first, fixed while active
        float arriveSec = 0.0f;             // patrol reaches the current fight, at decision time
        float benefit = 0.0f;               // points not lost to the patrol
        float cost = 0.0f;                  // points lost walking there
        uint32 sinceMs = 0;
        // Written by the tank's PatrolBehavior (same exception as PullPlan::rangedShot): index
        // of the spot that passed the live checks, -1 = none yet, -2 = none of them passed.
        mutable int32 chosen = -1;
        mutable Position3 chosenSpot;       // spots[chosen] with its ground height

        // Patrols already judged this fight (moved for, or staying put): one note each.
        std::vector<ObjectGuid> judged;
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

    // Enemy the damage dealers kill first (FocusAssigner). Empty enemy = no focus.
    struct FocusTarget
    {
        ObjectGuid enemy;
        float score = 0.0f;          // harmRate / killSec (FocusAssigner.h)
        float harmRate = 0.0f;       // points per second
        float killSec = 0.0f;
        std::string reason;
        uint32 sinceMs = 0;          // world refresh time it became the focus
    };

    // Must-kill enemy handled by one bot without moving the party focus (FocusAssigner).
    struct InterceptDuty
    {
        ObjectGuid enemy;
        ObjectGuid bot;
        float points = 0.0f;         // what it does if left alone (benefit of the switch)
        char const* reason = "";     // static string
    };

    // Role the "roles" assigner gives a bot whose role is not locked (RoleAssigner.h).
    struct RoleDuty
    {
        ObjectGuid bot;
        uint8 role = 0;              // CombatBotRoles
        char const* reason = "";     // static string
    };

    struct TeamPlan
    {
        uint32 version = 0;
        std::vector<RoleDuty> roles;
        std::vector<InterruptDuty> interrupts;
        FocusTarget focus;
        std::vector<InterceptDuty> intercepts;
        PullPlan pull;
        PullOutcome lastPullOutcome = PullOutcome::None;    // set when a pull ends, read by ClearPlanner
        ClearPlan clear;
        RetreatPlan retreat;
        PatrolMovePlan patrolMove;
        std::vector<std::string> notes;     // messages for the leader, flushed by PartyCommander

        InterruptDuty const* FindInterruptDuty(ObjectGuid enemy) const
        {
            for (InterruptDuty const& d : interrupts)
                if (d.enemy == enemy)
                    return &d;
            return nullptr;
        }

        InterceptDuty const* FindInterceptFor(ObjectGuid bot) const
        {
            for (InterceptDuty const& d : intercepts)
                if (d.bot == bot)
                    return &d;
            return nullptr;
        }
    };
}

#endif
