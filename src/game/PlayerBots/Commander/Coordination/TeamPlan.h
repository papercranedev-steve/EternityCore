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
#include "Commander/Perception/Values/ManaWorth.h"
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
    // beforeOpener (Sap, .kiro/docs/2026-11-08-sap.md): the rogue walks there stealthed and
    // casts before the tank opens; the tank waits until the Sap is on the target (landed).
    struct PullControlPlan
    {
        bool decided = false;           // Choose ran for this Approach (with or without a control)
        ObjectGuid bot;                 // empty = no opening control
        ObjectGuid target;
        uint32 spellId = 0;
        Position3 spot;
        float points = 0.0f;            // net worth (ControlValue::OfPull, SapValue::Of)
        bool beforeOpener = false;      // cast out of combat, before the tank opens (Sap)
        bool inPlace = false;           // the controller reached the spot: the tank may open (Sap: cast now)
        bool landed = false;            // beforeOpener: the target has the control
        bool dropped = false;           // not in place in time, or the controller cannot act
        uint32 sinceMs = 0;             // decided at
        uint32 placeTimeoutMs = 0;      // the walk there may take this long
        uint32 inPlaceMs = 0;           // reached the spot at

        bool Active() const { return decided && !bot.IsEmpty() && !dropped; }
        // The tank waits before opening: not decided yet, or the controller is still walking
        // (Sap: until it is on the target).
        bool HoldsOpener() const { return !decided || (Active() && (beforeOpener ? !landed : !inPlace)); }
    };

    struct PullPlan
    {
        PullPhase phase = PullPhase::None;
        ObjectGuid target;
        ObjectGuid tank;
        std::vector<ObjectGuid> expected;   // AssistPredictor result, target included
        Position3 spot;                     // where the party fights (leader position at command time)
        // The tank walks the pack back to `spot` (backwards, facing it: Behaviors/Movement/Backpedal.h),
        // decided by PullPlanner::Plan from the party's edge and the add risk (Perception/Forecast/PullOdds.h).
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
        // The rest of that estimate, for the combat review to compare with what happened
        // (review.lua "estimate"): the party damage it assumed (Value::DealtDps and where that
        // came from), the healer bots' mana before and after, the tank's lowest health.
        float expectedPartyDps = 0.0f;
        char const* expectedDpsSource = "";
        float expectedManaStart = 0.0f;
        float expectedManaLeft = 0.0f;
        float expectedLowestTankPct = 0.0f;
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
        // Where the tank shoots from (Perception/Spatial/ShotSpot: target in sight, outside every idle
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
        // target's aggro range and in its sight (Perception/Spatial/ShotSpot.h FindFace), and does
        // nothing there: the target sees it and comes. Not noticed within
        // PullBehavior::FACE_WAIT_MS: walks in.
        bool facePull = false;
        Position3 faceSpot;
        // Opens from a distance (shot or face pull): fought in place, the tank waits where it
        // opened (awaitArrival).
        bool OpensFromAfar() const { return rangedPullSpell || facePull; }

        // Fight at a corner (PullPlace.h). Where the party fights (here, brought back: returnToSpot,
        // corner) is the doctrine's choice (Coordination/Doctrine/FightPlace.h, FightPlace::Choose),
        // PullPlanner::Plan carries it out. Once the fight started, the doctrine has the tank hide
        // from the pulled enemies standing away from it (STAMP_LURE), not only from casters
        // standing back; SearchLosBreak looks for the point.
        bool corner = false;
        std::string placeWhy;               // why this place (FightPlace::Choice::why)

        // Opening control (Coordination/Situation/Pull/PullControl.h): one pulled creature controlled as the
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

        // Healing hold of a line of sight pull, written by PullAssigner during Settle once the
        // arrival wait is over (Coordination/Situation/Pull/PullOpening.h): out of combat healers wait until the
        // casters came (a heal's threat lands on every enemy at once). Who waits for the tank's
        // threat is the threat opening's (TeamPlan::threat, Coordination/Doctrine/ThreatOpening.h).
        bool holdHealing = false;

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
        // yet, or (healers) the casters of a line of sight pull have not come. The wait for the
        // tank's threat is TeamPlan::threat (every bot, in combat or not).
        bool HoldsWaiting(bool healer) const
        {
            switch (phase)
            {
                case PullPhase::Approach:
                case PullPhase::Return:
                    return true;
                case PullPhase::Settle:
                    return awaitArrival || (healer && holdHealing);
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

        // The pull predicted `e` (or `e` was summoned by a creature it predicted).
        bool Expects(EnemyFacts const& e) const
        {
            auto const has = [this](ObjectGuid g) { return std::find(expected.begin(), expected.end(), g) != expected.end(); };
            return has(e.guid) || (e.summoned && has(e.summoner));
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

    // A member stands (or walks to) where an idle patrol comes before we leave
    // (RouteGuardAssigner): it walks off the route now (OffRouteBehavior, Urgent).
    struct OffRouteTask
    {
        ObjectGuid bot;
        ObjectGuid patrol;          // the first patrol to come there
        float arriveSec = 0.0f;     // it comes in this many seconds (from the refresh)
        float leaveSec = -1.0f;     // grid walk to the nearest cell off the route, -1 = none found
        float points = 0.0f;        // what the patrol joining costs (RouteGuard.h)
    };

    struct RouteGuardPlan
    {
        float staySec = 0.0f;       // RouteGuard::StaySec of this refresh (for the logs)
        std::vector<OffRouteTask> tasks;

        OffRouteTask const* Find(ObjectGuid bot) const
        {
            for (OffRouteTask const& t : tasks)
                if (t.bot == bot)
                    return &t;
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
        float points = 0.0f;         // what removing it is worth (Perception/Values/DispelValue.h)
        bool control = false;        // the member cannot act: urgent
    };

    // One control spell to put on an enemy now, and who casts it (CrowdControlAssigner).
    struct ControlDuty
    {
        ObjectGuid enemy;            // the target; an area spell (radius around the caster): the first enemy it takes
        ObjectGuid bot;
        uint32 spellId = 0;
        float points = 0.0f;         // net worth (Perception/Values/ControlValue.h)
        bool area = false;
        bool breaksOnDamage = false;
    };

    // How one loose enemy is taken off its victim, and by whom (RescueAssigner).
    enum class RescueWay : uint8
    {
        Taunt,          // the tank taunts it (spellId), then holds it (TargetAssigner gives it the enemy)
        Threat,         // the tank walks over and wins it back with threat (its target)
        ThreatDrop,     // the victim drops its threat (Fade, spellId)
        Save,           // keep the victim alive (spellId on the victim: Shield Wall, a shield; no enemy)
    };

    struct RescueDuty
    {
        ObjectGuid enemy;
        ObjectGuid victim;
        ObjectGuid bot;
        RescueWay way = RescueWay::Threat;
        uint32 spellId = 0;          // Taunt, ThreatDrop
        float points = 0.0f;         // net worth (Perception/Values/RescueValue.h)
        bool urgent = false;         // the victim dies within the horizon: Urgent tier
    };

    // A stun, root or slow kept for an enemy about to run away (FleeStopAssigner, todo 13): a
    // reservation. Before it is due the bot holds the spell (HoldsSpell) and fights as usual;
    // FleeStopBehavior checks the trigger live every tick and casts.
    struct FleeStopDuty
    {
        ObjectGuid enemy;
        ObjectGuid bot;
        uint32 spellId = 0;
        float points = 0.0f;         // net worth (Perception/Values/FleeStopValue.h)
        bool hold = false;           // stun / root: cast when the party kills it within holdSec
        float holdSec = 0.0f;
        bool needsCombo = false;     // a finisher: its other finishers are held too
        float triggerPct = 0.0f;     // hold: cast from this health percent on (also once it runs)
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
        // Why (the tank's target reason): only controlled enemies left, or the doctrine's area
        // pile has one of ours held by a bot's control (ControlledEnd.h).
        char const* why = "only controlled enemies left: wake one";

        bool Breaks(ObjectGuid bot, ObjectGuid target, uint32 nowMs) const
        {
            return active && bot == breaker && target == enemy && int32(nowMs - breakAtMs) >= 0;
        }
    };

    // The area phase (Coordination/Doctrine/AreaPhase.h, .kiro/docs/2026-11-08-aoe-phase.md): one area
    // damage spell the party goes for now, who casts it, where from, and what is done first.
    // A reservation for the caster: AreaDamageBehavior walks, shields and casts in that order.
    enum class AreaStep : uint8
    {
        Move,       // walk to `spot` (farther from the pack: the enemies it turns take longer to come)
        Shield,     // put `shieldSpellId` on itself first
        Cast,
        Channel,    // running: the brain only keeps its value (endCost, the priest's shield)
    };

    struct AreaPlan
    {
        bool active = false;
        AreaStep step = AreaStep::Cast;
        ObjectGuid caster;
        uint32 spellId = 0;
        ObjectGuid centre;           // ground spell: the enemy it is centred on; empty = around the caster
        Position3 centrePos;
        Position3 spot;              // Move: where to cast from
        float moveBenefit = 0.0f;    // turn loss saved there
        float moveCost = 0.0f;       // the walk
        float netHere = 0.0f;        // the whole spell cast from where the caster stands (Move: the fallback)
        uint32 shieldSpellId = 0;    // Shield
        float shieldGain = 0.0f;
        // A healer bot shields the caster (Power Word: Shield): its turn loss is lower with it.
        ObjectGuid allyShieldBot;
        uint32 allyShieldSpellId = 0;
        float allyShieldGain = 0.0f;
        float benefit = 0.0f;        // of the whole spell from `spot` (or here)
        float cost = 0.0f;
        float net = 0.0f;
        // What ending the phase now loses (a tank sent elsewhere: PatrolAssigner pays it).
        float endCost = 0.0f;
        // The phase counts on the tanks' taunts for the enemies it turns: what it loses
        // without them (RescueAssigner adds it to a taunt spent on another enemy).
        float tauntReserve = 0.0f;
        std::vector<ObjectGuid> targets;
        uint32 sinceMs = 0;

        bool CastBy(ObjectGuid bot) const { return active && caster == bot; }
        bool Targets(ObjectGuid enemy) const
        {
            return active && std::find(targets.begin(), targets.end(), enemy) != targets.end();
        }
    };

    // ---- Doctrine (brain layer 2, Coordination/Doctrine/TacticalDoctrine.h; doc
    // .kiro/docs/2026-11-09-doctrine section 4): how this fight is fought, and the stamps on
    // the enemies the duties read.
    enum class FightPhase : uint8
    {
        None,       // no fight of ours
        Gathering,  // the pull or the threat opening still runs: groups change every refresh
        Clash,
        Cleanup,    // at most CLEANUP_FREE enemies not under a control
    };

    enum class PileTactic : uint8
    {
        None,       // not chosen (no fight, no main tank, a retreat)
        Area,       // the pile around the tank is left to area spells: no control that breaks on damage there
        Pick,       // the pile may be controlled, killed one by one
    };

    // Bits of DoctrineEnemy::stamps.
    enum DoctrineStamp : uint16
    {
        STAMP_IN_CLUSTER    = 1 << 0,   // the pile around the main tank (or a melee enemy on its way there)
        STAMP_OUTLIER       = 1 << 1,   // stands back away from the pile (casters)
        STAMP_SPECIAL       = 1 << 2,   // a heavy enemy not at the pile yet (a patrol walking in)
        STAMP_NO_MEZ        = 1 << 3,   // no control that breaks on damage, no fear (CrowdControlAssigner)
        STAMP_COMMITTED     = 1 << 4,   // held by a bot's control: that bot renews it, does not move it
        STAMP_PLAYER_TARGET = 1 << 5,   // a player hits it
        // The tank steps out of its sight so it walks to the tank (OutlierTactic::Lure; the pull's
        // map step SearchLosBreak looks for the point).
        STAMP_LURE          = 1 << 6,
        // Roles and sources (todo 9, .kiro/docs/2026-11-09-doctrine/2026-11-10-roles-sources.md):
        STAMP_SELF_DESTRUCT = 1 << 7,   // blows itself up (Broken Cadaver): no taunt, no rescue, no control
        STAMP_KILL_NOW      = 1 << 8,   // a self-destruct the party kills before its earliest blast: the focus
        STAMP_STAND_CLEAR   = 1 << 9,   // a self-destruct that will blow first: everyone but its victim steps out
        STAMP_SWARM         = 1 << 10,  // from a trap (crate insects): no control, no threat opening, the area spells'
        STAMP_ALARM         = 1 << 11,  // brings more as it aggroes: display (its summons are in FightLoad)
        STAMP_NO_TAUNT      = 1 << 12,  // no single target taunt (swarm, self-destruct, taunt immune, totem): RescueAssigner
        STAMP_KILL_ORDER    = 1 << 13,  // killed first (healer, summoner, totem, mana drain, resurrects): the focus is among these
        STAMP_HOLD_FIRE     = 1 << 14,  // a damage shield / spell reflection is on it: nobody hits it (EngageBlock, Constraints)
        STAMP_FREE_ALLY     = 1 << 15,  // it mind controls a party member: the focus, killed at once (TargetAssigner)
    };

    // A party member an enemy mind controls (Coordination/Doctrine/AllyCharm.h).
    struct CharmedAlly
    {
        ObjectGuid ally;
        ObjectGuid charmer;
        bool hold = false;          // a bot controls the ally until the charmer dies (CrowdControlAssigner)
        float killSec = 0.0f;       // the charmer's kill time (health / party damage)
        uint32 charmLeftMs = 0;
    };

    // The main tank's job under the tactic (TacticalDoctrine; todo 12, user 2026-11-10). Which
    // enemy and which ability stay the duties' and the cerebellum's (points).
    enum class TankJob : uint8
    {
        None,       // gathering (the threat opening's task), no tactic
        Focus,      // Pick: the tank hits the party focus too (TankTargets) when it is at the pile
        HoldPile,   // Area: the tank keeps the pile (top-up) and protects the area caster: a rescue
                    // of an enemy on it is Urgent (RescueAssigner), so is a group threat on two or
                    // more of them (ThreatBehavior: Challenging Shout)
    };

    // What the party does about the enemies away from the tank (casters standing back, the far
    // ones of a corner fight): Coordination/Doctrine/FightPlace.h, chosen by TacticalDoctrine.
    enum class OutlierTactic : uint8
    {
        None,
        Lure,       // the tank hides from them (line of sight) so they walk into the pile
        KillFirst,  // no luring: under the Area tactic the focus is among them first (melee go when the walk is safe)
    };

    struct DoctrineEnemy
    {
        ObjectGuid guid;
        uint16 stamps = 0;
        uint8 engage = 0;           // Engage (Perception/Profiles/Tags.h)
        uint8 tier = 0;             // Tier
        uint8 source = 0;           // Tags::Source
        uint16 roles = 0;           // Tags::RolesOf bits
        std::string noMezWhy;       // why STAMP_NO_MEZ ("area pile", "fodder", "cleanup", "player hits it")
    };

    struct DoctrinePlan
    {
        FightPhase phase = FightPhase::None;
        PileTactic tactic = PileTactic::None;
        uint32 sinceMs = 0;                 // tactic chosen at
        std::string why;                    // why this tactic, and why it was (re)chosen
        uint32 areaDealers = 0;             // TeamLedger::areaDealers when chosen
        std::vector<ObjectGuid> known;      // the enemies of the fight when chosen: one not in it is an add
        TankJob tankJob = TankJob::None;
        OutlierTactic outliers = OutlierTactic::None;
        std::string outliersWhy;
        bool lureCorner = false;            // Lure for a corner fight (not casters standing back)
        std::vector<CharmedAlly> charmed;
        std::vector<DoctrineEnemy> enemies;

        DoctrineEnemy const* Find(ObjectGuid enemy) const
        {
            for (DoctrineEnemy const& d : enemies)
                if (d.guid == enemy)
                    return &d;
            return nullptr;
        }
        bool Has(ObjectGuid enemy, uint16 stamp) const
        {
            DoctrineEnemy const* d = Find(enemy);
            return d && (d->stamps & stamp) != 0;
        }
    };

    // The enemy one bot attacks (TargetAssigner, executed by TargetBehavior). A bot without an
    // entry attacks nothing new: healers, bots held by a pull, bots with no safe enemy.
    struct TargetDuty
    {
        ObjectGuid bot;
        ObjectGuid enemy;
        // Switching from the bot's current target to `enemy` is worth `benefit` and costs `cost`
        // (points, Perception/Values/SwitchCost.h). 0 / 0 when `enemy` already is its target.
        float benefit = 0.0f;
        float cost = 0.0f;
        bool urgent = false;         // Urgent tier (intercept, a loose enemy about to kill someone)
        bool forced = false;         // a player order or a brain task: Urgent, not valued
        char const* reason = "";     // static string
        uint32 sinceMs = 0;          // world refresh time the bot got this enemy
        // A suggestion until this WorldTimer ms (0 = switch now, a task): the bot may finish
        // what it is doing first (spend its combo points on the current target; a cast is
        // finished anyway), then switch; from then on it switches (TargetBehavior). Set for an
        // enemy about to run away (todo 13, TargetAssigner.h).
        uint32 suggestUntilMs = 0;
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

    // One enemy next to the tank it still lacks threat on (Perception/Forecast/ThreatGap.h).
    struct ThreatGapEntry
    {
        ObjectGuid enemy;
        float gap = 0.0f;
    };

    // Threat opening ("群拉", Coordination/Doctrine/ThreatOpening.h, .kiro/docs/2026-11-08-threat-modes.md):
    // after a pull and when new enemies reach the tank, the tank first gets enough threat on
    // each enemy next to it to keep it through the healer's next heal; everyone else waits.
    // A constraint while `active` (every cast of a held bot, its attacks: Behaviors/Constraints.h)
    // and the tank's task (TankTargets, ThreatBehavior value filling `gaps`).
    struct ThreatOpeningPlan
    {
        bool active = false;
        ObjectGuid tank;
        uint32 sinceMs = 0;
        std::vector<ThreatGapEntry> gaps;       // enemies next to the tank still short, while active
        // Enemies whose opening is over (their gap was filled once, or the opening gave up on
        // them): later thin leads are the top-up's (TankTargets), not a new opening.
        std::vector<ObjectGuid> settled;
        float bestGap = 0.0f;                   // smallest total gap this opening (progress)
        uint32 progressMs = 0;                  // last time the total gap shrank
        // Points per second the waiting damage dealers lose / the tank's threat per second on
        // the enemy it hits (ThreatGap::WaitCostPerSec, FillRate): what filling a gap is worth.
        float waitPerSec = 0.0f;
        float fillRate = 1.0f;
        // Members a held bot may act for: someone about to die (SurvivalValue danger, or below
        // PullBehavior::HOLD_HEAL_BELOW_PCT): healers heal, that member saves itself.
        bool healAllowed = false;
        std::vector<ObjectGuid> freed;          // held bots allowed to act anyway (in danger)

        // `bot` waits for the tank (casts nothing, attacks nothing).
        bool Holds(ObjectGuid bot, bool healer) const
        {
            if (!active || bot == tank)
                return false;
            if (healer && healAllowed)
                return false;
            return std::find(freed.begin(), freed.end(), bot) == freed.end();
        }
        float GapOf(ObjectGuid enemy) const
        {
            for (ThreatGapEntry const& g : gaps)
                if (g.enemy == enemy)
                    return g.gap;
            return 0.0f;
        }
        bool Settled(ObjectGuid enemy) const
        {
            return std::find(settled.begin(), settled.end(), enemy) != settled.end();
        }
    };

    struct TeamPlan
    {
        uint32 version = 0;
        std::vector<RoleDuty> roles;
        std::vector<InterruptDuty> interrupts;
        // Last DutyAllocator result of all duty modules (PartyCommander::RunAssigners), kept for
        // `.partybot ai plan` (who was passed over and why). Modules read their own part above.
        std::vector<DutyResult> duties;
        // The needs as offered this refresh, before the allocation (rejected candidates
        // included): for the combat recorder (u:offers, `offers` lines). Nobody reads it to decide.
        std::vector<DutyNeed> offers;
        FocusTarget focus;          // TargetAssigner (encounters may override it: EncounterHelpers::SetFocus)
        std::vector<TargetDuty> targets;
        PlayerOrders orders;
        std::vector<InterceptDuty> intercepts;
        std::vector<DispelDuty> dispels;
        std::vector<ControlDuty> controls;
        std::vector<RescueDuty> rescues;
        std::vector<FleeStopDuty> fleeStops;
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
        RouteGuardPlan routeGuard;
        AreaPlan area;
        ThreatOpeningPlan threat;
        DoctrinePlan doctrine;
        // The active boss module forbids eating and drinking (IEncounter::ForbidsRest; written
        // by PartyCommander every refresh). Read through ForbidsRest.
        bool noRest = false;
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

        FleeStopDuty const* FindFleeStopFor(ObjectGuid bot) const
        {
            for (FleeStopDuty const& d : fleeStops)
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

        // ---- Constraints (directive types doc, .kiro/docs/2026-11-07/2026-11-07-directive-types.md).
        // What the brain forbids. Every module reads them when it builds a proposal (to pick
        // something else), the arbiter drops a proposal that breaks one (Behaviors/Constraints.h)
        // and the class rotations ask the same queries (PartyBotAI::IsEngageForbidden,
        // IsGapCloserForbidden). The exceptions live here, not at the readers.

        // `bot` was given `enemy` to kill by the brain (intercept, also a caster of
        // patrolMove.kill). This refresh's allocation.
        bool AssignedToKill(ObjectGuid bot, ObjectGuid enemy) const
        {
            if (bot.IsEmpty())
                return false;
            for (DutyResult const& r : duties)
                if (r.kind == DutyKind::Intercept && r.primary == bot && r.target == enemy)
                    return true;
            return false;
        }

        // Why `bot` must not attack, chase or target `enemy`, or null. Empty bot = a party-wide
        // choice (focus, rescue): nobody's exception counts. Casting at it from where the bot
        // stands (an interrupt, a control) is not forbidden by this.
        char const* EngageBlock(ObjectGuid bot, ObjectGuid enemy) const
        {
            if (orders.LeavesAlone(enemy))
                return "player: leave it alone";
            // Doctrine (RoleStamps.h, user 2026-11-10): it hurts or reflects whoever hits it, everyone.
            if (doctrine.Has(enemy, STAMP_HOLD_FIRE))
                return "it reflects damage: hands off";
            // Standing in a patrol's path (PatrolAssigner.h): the party stays off it.
            if (patrolMove.Avoids(enemy) && !AssignedToKill(bot, enemy))
                return "in a patrol's path";
            return nullptr;
        }
        bool MayEngage(ObjectGuid bot, ObjectGuid enemy) const { return !EngageBlock(bot, enemy); }

        // No Charge / Intercept / Feral Charge for `bot`: the pull's tank going out to bring the
        // pack back or to a corner, walking back, or waiting where it shot for the pack to come.
        // Was a veto PullBehavior proposed every tick (VetoType::GapCloser) until 2026-11-07.
        bool ForbidsGapCloser(ObjectGuid bot) const
        {
            if (bot.IsEmpty() || bot != pull.tank)
                return false;
            switch (pull.phase)
            {
                case PullPhase::Approach: return pull.returnToSpot || pull.corner;
                case PullPhase::Return:   return true;
                case PullPhase::Settle:   return pull.awaitArrival && pull.losBreak != PullLosBreak::Active;
                default:                  return false;
            }
        }

        // `bot` keeps `spellId` for a flee stop (todo 13): nobody else casts it, the rotation
        // neither (PartyBotAI::IsSpellHeld), only FleeStopBehavior. A finisher kept (Kidney
        // Shot) holds the bot's other finishers too (`finisher`: the spell needs combo
        // points), so the points stay for it.
        bool HoldsSpell(ObjectGuid bot, uint32 spellId, bool finisher) const
        {
            FleeStopDuty const* d = FindFleeStopFor(bot);
            return d && (d->spellId == spellId || (finisher && d->needsCombo));
        }

        // `bot` must not sit down to eat or drink: a retreat runs, the boss module says so, or it
        // is the pull's tank and on its way.
        bool ForbidsRest(ObjectGuid bot) const
        {
            if (retreat.active || noRest)
                return true;
            return !bot.IsEmpty() && bot == pull.tank && pull.phase != PullPhase::None &&
                   pull.phase != PullPhase::WaitReady;
        }
    };
}

#endif
