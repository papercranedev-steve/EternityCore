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
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Coordination/DutyAllocator.h"
#include <algorithm>
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
        bool missingAmmoNoted = false;  // "No ranged pull: ... no arrows" said once per clear
    };

    // Display name of a party member, captured when the pull is planned (the pure assigner
    // has no live objects to read names from).
    struct PullMemberName
    {
        ObjectGuid guid;
        std::string name;
    };

    // The tank's steps of one Approach (PullBehavior): the walk to the shot or face pull spot,
    // the shots, and when the opener went out. A new Approach (phaseSinceMs changed: held back
    // and out again, or the brain gave a new spot or target) starts over.
    struct PullShotState
    {
        uint32 approachSinceMs = 0;     // PullPlan::phaseSinceMs of its Approach
        // The opener went out: the pull spell or Charge was cast (BotDirectives::GetLastExecuted),
        // the tank stands on its face pull spot, or it is inside the target's aggro range. From
        // then on the pull cannot be called off; PullAssigner no longer ends it as Interrupted
        // when an enemy it did not expect shows up first (strat-20261107-152527: two Plagued
        // Insects from a Stratholme crate reached the tank 0.4 s before the charged target did).
        uint32 openedAtMs = 0;
        Position3 standAt;              // PullPlan::rangedSpot / faceSpot, or the retry step's point
        uint32 walkSinceMs = 0;         // first walk order to standAt, 0 = none yet
        uint32 walkBudgetMs = 0;        // standAt not reached by then: spot failed (unreachable)
        uint32 shotAtMs = 0;            // first time in position, or the end of the retry walk; 0 = none yet
        bool retried = false;           // stepped RANGED_RETRY_STEP_YD closer once
        bool walkIn = false;            // gave up on the shot: walk-in pull
        // The tank stands on standAt but cannot shoot from there (no line of sight, out of
        // range: the target wandered), or cannot get there. It holds and does not step closer;
        // PullPlanner::RecheckBeforeApproach (brain, map thread) gives another spot or target.
        bool spotFailed = false;
        char const* spotFailWhy = "";   // static text for the note
    };

    // Line of sight pull (PullPlanner::SearchLosBreak, PullAssigner Settle).
    enum class PullLosBreak : uint8
    {
        None,       // not searched yet (no caster standing back seen in Settle so far)
        NoPoint,    // searched: no usable point, fight where we are
        Active,     // the tank stands at losPoint until the casters come or the timeout
        Ended,
    };

    // A healer bot hiding from the casters of a line of sight pull too (PullPlanner::SearchLosBreak).
    struct PullHider
    {
        ObjectGuid bot;
        Position3 point;
    };

    // Opening control of a pull (PullControl::Choose on the map thread, PullControl::Update in
    // PullAssigner): `bot` stands at `spot` (in range and sight of `target`, out of every idle
    // creature's aggro range) before the tank opens, then casts `spellId` on `target` once the
    // fight started (CrowdControlAssigner offers it, CrowdControlBehavior casts it).
    struct PullControlPlan
    {
        bool decided = false;           // Choose ran for this Approach (with or without a control)
        ObjectGuid bot;                 // empty = no opening control
        ObjectGuid target;
        uint32 spellId = 0;
        Position3 spot;
        float points = 0.0f;            // net worth (ControlValue::OfPull)
        bool inPlace = false;           // the controller reached the spot: the tank may open
        bool dropped = false;           // not in place in time, or the controller cannot act
        uint32 sinceMs = 0;             // decided at

        bool Active() const { return decided && !bot.IsEmpty() && !dropped; }
        // The tank waits before opening: not decided yet, or the controller is still walking.
        bool HoldsOpener() const { return !decided || (Active() && !inPlace); }
    };

    struct PullPlan
    {
        PullPhase phase = PullPhase::None;
        ObjectGuid target;
        ObjectGuid tank;
        std::vector<ObjectGuid> expected;   // AssistPredictor result, target included
        Position3 spot;                     // where the party fights (leader position at command time)
        // The tank walks the pack back to `spot` (backwards, facing it: Behaviors/Backpedal.h),
        // decided by PullPlanner::Plan from the party's edge and the add risk (Perception/PullOdds.h).
        bool returnToSpot = false;
        uint32 returnTimeoutMs = 0;         // Return ends by then; set when Return starts (walk length)
        uint32 phaseSinceMs = 0;
        // The pull target's position when planned: the enemies come from there (the stance
        // modules place healers and ranged on the far side of the fight spot).
        Position3 packAt;
        // FightLoad estimate of the planned fight: damage per second on the tank (0 = none).
        // Values a bot's preparation before the fight (Value::ContributionPerSecAt).
        float expectedIncomingDps = 0.0f;
        float expectedFightSec = 0.0f;      // same estimate: how long the fight lasts
        // The estimate as ManaWorth's planned fight while the pull runs; none (sec 0) otherwise.
        ManaWorth::Fight PlannedFight() const
        {
            ManaWorth::Fight f;
            if (phase != PullPhase::None)
            {
                f.sec = expectedFightSec;
                f.incomingDps = expectedIncomingDps;
            }
            return f;
        }

        // WaitReady bookkeeping.
        // Healer mana needed before the tank goes: from the fight estimate (PullPlanner::Plan,
        // READY_HEALER_*), READY_MANA_PCT without one.
        float healerManaPct = 80.0f;
        uint32 botWaitMs = 0;               // time bots were not ready; only this counts toward the timeout
        uint32 lastReadyCheckMs = 0;
        bool readyNoteSent = false;         // "still waiting for ..." is sent once
        bool readyAnnounced = false;        // "everyone ready" is sent once
        std::vector<PullMemberName> names;

        // What PullPlanner::Plan told the player (target, expected, ranged or not and why,
        // fight there or bring back, patrols), for the combat recorder (u:pull()).
        uint32 plannedAtMs = 0;
        std::vector<std::string> report;

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
        // Where the tank shoots from (Perception/ShotSpot: target in sight, outside every idle
        // creature's aggro range, safe walk). Found by PullPlanner::Plan, looked for again
        // before the tank goes and when the tank reports the spot failed (rangedShot.spotFailed).
        Position3 rangedSpot;
        std::vector<Position3> failedSpots;     // of this target, not offered again
        uint32 spotFailures = 0;                // of this target
        // Pack members given up as targets because no spot works (PullPlanner picks another).
        std::vector<ObjectGuid> excludedTargets;
        // Written by the tank's PullBehavior::Propose, the one exception to "behaviors only
        // read the plan": only the behavior sees when the tank stands in range with line of
        // sight and whether it is still on a held walk (live checks, every bot tick, not in
        // the facts). The plan belongs to one PartyCommander, which only its map thread
        // touches, so no lock is needed. Reset with the plan (new pull) and by a new Approach.
        // The brain only reads it (spotFailed, standAt) and answers by changing rangedSpot or
        // the target and starting a new Approach.
        mutable PullShotState rangedShot;

        // Face pull (PullMethod::Face, or chosen by Auto): the tank walks to faceSpot, inside the
        // target's aggro range and in its sight (Perception/ShotSpot.h FindFace), and does
        // nothing there: the target sees it and comes. Not noticed within
        // PullBehavior::FACE_WAIT_MS: walks in.
        bool facePull = false;
        Position3 faceSpot;
        // Opens from a distance (shot or face pull): fought in place, the tank waits where it
        // opened (awaitArrival).
        bool OpensFromAfar() const { return rangedPullSpell || facePull; }

        // Fight at a corner (PullPlace.h): planned by PullPlanner::Plan (the player chose it, or
        // the fight estimate is hard). Once the fight started, SearchLosBreak hides the tank
        // from the pulled enemies standing away from it, not only from casters standing back.
        bool corner = false;

        // Opening control (Coordination/PullControl.h): one pulled creature controlled as the
        // tank opens, the controller standing ready before. Decided when the tank is about to go.
        PullControlPlan control;

        // Enemies that were not expected and joined after the opener went out (openedAtMs).
        // Added to `expected` too: the opening holds and the line of sight pull count them.
        std::vector<ObjectGuid> extra;

        // Ranged pull fought where the pack stands (not brought back): Settle starts with the
        // tank staying at arrivalPoint (where it shot) until the pack came, so the party does
        // not meet the pack halfway, next to the creatures the ranged pull was meant to leave
        // alone. PullAssigner ends the wait (PackArrived, arrivalTimeoutMs after Settle began,
        // a line of sight pull taking over) and starts Settle's own time then.
        bool awaitArrival = false;
        Position3 arrivalPoint;
        uint32 arrivalTimeoutMs = 0;

        // Line of sight pull. A pulled caster standing back casting at the tank (melee off, or
        // a main ranged spell and not moving) more than PullPlanner::LOS_CASTER_MIN_DISTANCE
        // away: during Settle the tank moves to a nearby point the caster cannot see. Its main
        // ranged spell then fails (line of sight) and the creature AI turns chase back on
        // (CreatureAI::DoSpellsListCasts), so it walks to the tank. Searched once per pull.
        PullLosBreak losBreak = PullLosBreak::None;
        Position3 losPoint;
        std::vector<ObjectGuid> losCasters;  // casters the point hides from
        uint32 losSinceMs = 0;              // Active since then (PullAssigner::LOS_BREAK_TIMEOUT_MS)
        // Healer bots that hide too while the break is Active: out of the casters' sight, in
        // sight of losPoint, and placed so that a caster walking to the healer passes the tank
        // (a heal that pulls it brings it to the tank anyway). A healer without a point stays
        // (HealerStanceBehavior moves it when it loses the tank).
        std::vector<PullHider> losHiders;

        // Opening holds, written by PullAssigner during Settle once the arrival wait is over
        // (PullAssigner.h "Opening threat"). Out of combat bots wait while set (PullBehavior):
        // damage dealers until the tank has threat on every pulled enemy next to it, healers
        // also until the casters of a line of sight pull came (healing is the last to start:
        // its threat lands on every enemy at once).
        bool holdDamage = false;
        bool holdHealing = false;
        uint32 threatSinceMs = 0;           // first pulled enemy reached the tank, 0 = none yet

        // Where the tank will fight once the pack is on it, as planned, for the members to take
        // their places before (stance modules): a walk-in fought where the pack stands is fought
        // at packAt (the stance modules keep out of the pack's aggro range, it is still idle).
        // False = the fight is already on (Settle past the arrival wait: the tank's position).
        bool PlannedFightSpot(Position3& out) const
        {
            if (phase == PullPhase::Settle)
            {
                if (losBreak == PullLosBreak::Active)
                    out = losPoint;
                else if (awaitArrival)
                    out = arrivalPoint;
                else
                    return false;
                return true;
            }
            // (WaitReady too: members take their places, then eat and drink there)
            if (phase != PullPhase::WaitReady && phase != PullPhase::Approach && phase != PullPhase::Return)
                return false;
            if (returnToSpot)
                out = spot;
            else if (rangedPullSpell)
                out = rangedSpot;
            else if (facePull)
                out = faceSpot;
            else
                out = packAt;
            return true;
        }

        // Members other than the tank that are still out of combat wait while true (PullBehavior
        // holds them; healers and ranged first take their places for the planned fight,
        // PlannedFightSpot): the tank goes out or brings the pack back, the pack has not arrived
        // yet, or the opening hold of their kind (damage / healing) is on.
        bool HoldsWaiting(bool healer) const
        {
            switch (phase)
            {
                case PullPhase::Approach:
                case PullPhase::Return:
                    return true;
                case PullPhase::Settle:
                    return awaitArrival || (healer ? holdHealing : holdDamage);
                default:
                    return false;
            }
        }

        PullHider const* LosHiderOf(ObjectGuid bot) const
        {
            if (losBreak != PullLosBreak::Active)
                return nullptr;
            for (PullHider const& h : losHiders)
                if (h.bot == bot)
                    return &h;
            return nullptr;
        }
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
    // passes the live ground / idle creature / path checks (PatrolBehavior); the enemies
    // follow the tank, the followers below walk the same shift.
    //
    // Every other bot (healer, ranged, melee): healers and ranged keep their place in range
    // (HealerStanceBehavior, the 25 yd caster chase), melee stay on their target, so they
    // would stay in the patrol's path (strat-20261102-225354: the rogue stayed with two
    // casters at the old spot). It walks start + (tank's chosen spot - tankStart), the
    // position the assigner already checked against the patrol's walk.
    struct PatrolFollower
    {
        ObjectGuid guid;
        Position3 start;
        // Written by the follower's PatrolBehavior: 0 = not checked yet, 1 = target passed
        // the live checks, -1 = it did not (the follower stays, its stance module moves it).
        mutable int8 state = 0;
        mutable Position3 target;
    };

    enum class PatrolPhase : uint8
    {
        Move,       // the tank and the followers walk to the chosen spot
        Guard,      // arrived: nobody goes back into the path until the patrol walked past
    };

    enum class PatrolLureState : uint8
    {
        Search,     // PatrolPlanner::Step looks for a point on the map thread
        Active,     // the hider stands at `point` until the casters came (PatrolAssigner)
        NoPoint,    // no point found: the casters are killed from range instead
        Done,       // the casters came out (or died, or turned to someone else)
        Failed,     // they did not come within LURE_TIMEOUT_MS: killed from range instead
    };

    // Casters left standing in the patrol's path, casting at `hider` (their victim, a bot):
    // the hider steps out of their sight (LosBreak.h) and keeps fighting what it can reach
    // there, so they walk out of the path to it. One lure per hider and fight.
    struct PatrolLure
    {
        ObjectGuid hider;
        std::vector<ObjectGuid> casters;
        PatrolLureState state = PatrolLureState::Search;
        Position3 point;
        uint32 sinceMs = 0;                 // Active since then
    };

    struct PatrolMovePlan
    {
        bool active = false;
        PatrolPhase phase = PatrolPhase::Move;
        ObjectGuid patrol;
        ObjectGuid tank;
        Position3 tankStart;                // the tank's position at decision time
        std::vector<PatrolFollower> followers;
        std::vector<Position3> spots;       // best first, fixed while active
        float arriveSec = 0.0f;             // patrol reaches the current fight, at decision time
        float benefit = 0.0f;               // points not lost to the patrol
        float cost = 0.0f;                  // points lost walking there
        uint32 sinceMs = 0;
        uint32 guardSinceMs = 0;
        // Written by the tank's PatrolBehavior (same exception as PullPlan::rangedShot): index
        // of the spot that passed the live checks, -1 = none yet, -2 = none of them passed.
        mutable int32 chosen = -1;
        mutable Position3 chosenSpot;       // spots[chosen] with its ground height

        // Enemies standing where the patrol walks before the fight ends, every refresh while
        // active. Nobody chases them (PartyBotAI::BeginChasing), the tank does not walk over
        // to win them back and nobody is given them as a target (TargetAssigner), unless a bot
        // was given the enemy to kill (`kill`). Melee enemies walk out by themselves.
        std::vector<ObjectGuid> avoid;
        // Guard only. Lures: written by PatrolAssigner, the point by PatrolPlanner::Step.
        std::vector<PatrolLure> lures;
        // Casters of `avoid` no lure brings out (no point, timed out, victim not a bot): a
        // ranged damage dealer kills each (FocusAssigner intercept; since 2026-11-03 it runs after PatrolAssigner and reads this in the same refresh).
        std::vector<ObjectGuid> kill;

        // Patrols already judged this fight (moved for, or staying put): one note each.
        std::vector<ObjectGuid> judged;

        bool Avoids(ObjectGuid enemy) const
        {
            return active && std::find(avoid.begin(), avoid.end(), enemy) != avoid.end();
        }

        // The lure `bot` hides for, still going (Active), or null.
        PatrolLure const* ActiveLureOf(ObjectGuid bot) const
        {
            if (!active)
                return nullptr;
            for (PatrolLure const& l : lures)
                if (l.hider == bot && l.state == PatrolLureState::Active)
                    return &l;
            return nullptr;
        }
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

    // One debuff to remove from a party member, and who does it (DispelAssigner).
    struct DispelDuty
    {
        ObjectGuid target;           // the party member
        uint32 auraSpellId = 0;      // the debuff
        ObjectGuid bot;
        uint32 spellId = 0;          // the dispel it casts
        float points = 0.0f;         // what removing it is worth (Perception/DispelValue.h)
        bool control = false;        // the member cannot act: urgent
    };

    // One control spell to put on an enemy now, and who casts it (CrowdControlAssigner).
    struct ControlDuty
    {
        ObjectGuid enemy;            // the target; an area spell (radius around the caster): the first enemy it takes
        ObjectGuid bot;
        uint32 spellId = 0;
        float points = 0.0f;         // net worth (Perception/ControlValue.h)
        bool area = false;
        bool breaksOnDamage = false;
    };

    // How one loose enemy is taken off its victim, and by whom (RescueAssigner).
    enum class RescueWay : uint8
    {
        Taunt,          // the tank taunts it (spellId), then holds it (TargetAssigner gives it the enemy)
        Threat,         // the tank walks over and wins it back with threat (its target)
        ThreatDrop,     // the victim drops its threat (Fade, spellId)
    };

    struct RescueDuty
    {
        ObjectGuid enemy;
        ObjectGuid victim;
        ObjectGuid bot;
        RescueWay way = RescueWay::Threat;
        uint32 spellId = 0;          // Taunt, ThreatDrop
        float points = 0.0f;         // net worth (Perception/RescueValue.h)
        bool urgent = false;         // the victim dies within the horizon: Urgent tier
    };

    // Only controlled enemies are left (ControlledEndAssigner): wait for mana while they sleep,
    // then the tank wakes the first one (attacks it, PartyBotAI::IsAssignedToBreakControl).
    struct ControlEndPlan
    {
        bool active = false;
        ObjectGuid enemy;            // the one to wake first (its control ends soonest)
        ObjectGuid breaker;          // the main tank
        uint32 breakAtMs = 0;        // wake it from then on (now = no wait)
        float waitGain = 0.0f;       // points of mana regained while waiting
        float waitCost = 0.0f;       // points of time and risk
        bool noted = false;          // the leader was told

        bool Breaks(ObjectGuid bot, ObjectGuid target, uint32 nowMs) const
        {
            return active && bot == breaker && target == enemy && int32(nowMs - breakAtMs) >= 0;
        }
    };

    // The enemy one bot attacks (TargetAssigner, executed by TargetBehavior). A bot without an
    // entry attacks nothing new: healers, bots held by a pull, bots with no safe enemy.
    struct TargetDuty
    {
        ObjectGuid bot;
        ObjectGuid enemy;
        // Switching from the bot's current target to `enemy` is worth `benefit` and costs `cost`
        // (points, Perception/SwitchCost.h). 0 / 0 when `enemy` already is its target.
        float benefit = 0.0f;
        float cost = 0.0f;
        bool urgent = false;         // Urgent tier (intercept, a loose enemy about to kill someone)
        bool forced = false;         // a player order or a brain task: Urgent, not valued
        char const* reason = "";     // static string
        uint32 sinceMs = 0;          // world refresh time the bot got this enemy
    };

    // What the player told the bots to attack (`.partybot attackstart`, `.partybot pull`) or to
    // leave alone (`.partybot attackstop`). Input written by those commands, read by
    // TargetAssigner, which also ends them (enemy gone / fight over).
    struct PlayerOrders
    {
        ObjectGuid attack;           // every bot (tankOnly: tanks only) attacks it
        bool attackTanksOnly = false;
        uint32 attackSinceMs = 0;
        std::vector<ObjectGuid> leaveAlone;     // not attacked until the fight is over

        bool LeavesAlone(ObjectGuid enemy) const
        {
            return std::find(leaveAlone.begin(), leaveAlone.end(), enemy) != leaveAlone.end();
        }
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
        // Last DutyAllocator result of all duty modules (PartyCommander::RunAssigners), kept for
        // `.partybot ai plan` (who was passed over and why). Modules read their own part above.
        std::vector<DutyResult> duties;
        FocusTarget focus;          // TargetAssigner (encounters may override it: EncounterHelpers::SetFocus)
        std::vector<TargetDuty> targets;
        PlayerOrders orders;
        std::vector<InterceptDuty> intercepts;
        std::vector<DispelDuty> dispels;
        std::vector<ControlDuty> controls;
        std::vector<RescueDuty> rescues;
        // Constraint for every area spell (AreaDamageBehavior, the tank's area threat in
        // ThreatBehavior): enemies about to get a control that breaks on damage
        // (CrowdControlAssigner). Enemies already under one are kept by KeepsUnbroken from the facts.
        std::vector<ObjectGuid> protectedEnemies;
        ControlEndPlan controlEnd;
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

        TargetDuty const* FindTargetFor(ObjectGuid bot) const
        {
            for (TargetDuty const& d : targets)
                if (d.bot == bot)
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

        RescueDuty const* FindRescueFor(ObjectGuid bot) const
        {
            for (RescueDuty const& d : rescues)
                if (d.bot == bot)
                    return &d;
            return nullptr;
        }

        ControlDuty const* FindControlFor(ObjectGuid bot) const
        {
            for (ControlDuty const& d : controls)
                if (d.bot == bot)
                    return &d;
            return nullptr;
        }

        // An area spell must leave `e` alone: it is under a control that breaks on damage
        // (anyone's, a player's sheep too), or the brain is about to put one on it.
        bool KeepsUnbroken(EnemyFacts const& e) const
        {
            if (e.control.spellId && e.control.breaksOnDamage)
                return true;
            return std::find(protectedEnemies.begin(), protectedEnemies.end(), e.guid) != protectedEnemies.end();
        }
    };
}

#endif
