/*
 * PartyBot Commander layer 2 (team assignment): the party plan.
 * Plain data written by assigners, read by behaviors. Map thread only.
 * Exception: PullPlan::rangedShot (mutable) is kept by the tank's PullBehavior.
 */

#ifndef MANGOS_COMMANDER_TEAM_PLAN_H
#define MANGOS_COMMANDER_TEAM_PLAN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Forecast/ThreatRunway.h"
#include "Commander/Perception/Profiles/StandIn.h"
#include "Commander/Perception/Spatial/WayIn.h"
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
        // The tank stopped before the opener because the plan no longer fits (PullAssigner's
        // walk guard: the walk passes creatures the prediction did not count, the target
        // walked off). `.partybot ai clear` plans the next pull from where things are now.
        CalledOff,
    };

    // `.partybot ai push`: the tank leads the party through the dungeon on its own (PushPlanner).
    enum class PushPhase : uint8
    {
        Clear,      // a clear runs from where the guide stands
        Advance,    // the guide walks towards the next goal, a leg at a time
    };

    struct PushPlan
    {
        bool active = false;
        PushPhase phase = PushPhase::Clear;
        float radius = 40.0f;           // of each clear
        uint32 maxMobs = 4;
        ObjectGuid guideBefore;         // TeamPlan::guide before the push, restored at the end

        // Goal: a hostile spawn still alive (SpawnTargets), empty = pick one.
        ObjectGuid goal;
        Position3 goalPos;
        uint32 goalFailures = 0;        // legs to it that did not get there
        std::vector<ObjectGuid> givenUp;    // goals reached without a fight or not reachable

        // The leg being walked (PushBehavior walks the guide there).
        bool walking = false;
        Position3 legEnd;
        uint32 legDeadlineMs = 0;
        Position3 lastPos;              // stuck check: the guide's last position that counted as a move
        uint32 lastMovedMs = 0;

        uint32 cleared = 0;             // packs over all clears
        uint32 clearedLastRound = 0;    // packs the last clear killed
        ObjectGuid lastBlocker;         // the creature the last leg stopped before
        std::string waitNoted;          // the last "waiting for ..." said, said once
    };

    // Why the last clear ended (ClearPlanner::Stop), for the push mode.
    enum class ClearEnd : uint8
    {
        None,
        NothingLeft,    // no pack left in range it would pull
        Failed,         // anything else: a pull failed, deaths, no tank, stopped, retreating
    };

    // A team of bots with no player (Commander/Team.h: a field bot alone) finds its own fights
    // (Situation/Hunt/HuntAssigner): the next creature to attack, kept until the fight starts,
    // it dies, or it is no longer a good choice. HuntBehavior attacks it.
    struct HuntPlan
    {
        ObjectGuid target;
        uint32 sinceMs = 0;         // WorldModel::refreshedAtMs of the choice
        uint32 idleSinceMs = 0;     // nothing to hunt nearby since (0 = something was found)
        char const* why = "";       // static text
        bool Active() const { return !target.IsEmpty(); }
    };

    // `.partybot ai clear`: pull nearby packs one after another (ClearPlanner).
    struct ClearPlan
    {
        bool active = false;
        ClearEnd lastEnd = ClearEnd::None;
        // Pauses while the leader walks away from the anchor (`.partybot ai clear`). The push
        // mode's clears do not: the push planner moves the party, the anchor is the guide's spot.
        bool followsLeader = true;
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
        // First member of packs in range by straight line but with no walkable way from the
        // leader (PackRouteState::NoWay), already reported. Looked at again every round.
        std::vector<ObjectGuid> noWayNoted;
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
        // The patrol check's third way (PullPatrolCheck ChooseStart, battlefield groups doc 5):
        // pull now, `holdBot` holds the patrol `holdPatrol` with `holdSpellId` when it comes
        // (the doctrine's hold, TacticCheck.h). Empty = none.
        ObjectGuid holdPatrol;
        ObjectGuid holdBot;
        uint32 holdSpellId = 0;

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

        // Ambush of a patrol (Perception/Spatial/AmbushSpot.h, chosen in PullChoice): a face
        // pull whose faceSpot lies on the patrol's way. The tank walks there and waits until the
        // patrol comes by, never walks after it (FACE_WAIT_MS and the walk-in fallbacks do not
        // apply). Approach ends by ambushTimeoutMs (from the Approach's start: the predicted
        // arrival + PullAssigner::AMBUSH_EXTRA_MS). ambushWaitSinceMs: the recheck found no
        // spot on the patrol's way since then (held in WaitReady), 0 = not waiting.
        bool ambush = false;
        uint32 ambushTimeoutMs = 0;
        uint32 ambushWaitSinceMs = 0;

        // The tank left Approach without a fight (cancelled, interrupted, called off): it still
        // has the target as its victim, and the original AI would chase it. PullBehavior drops
        // it (BotActionType::DropTarget) while out of combat, until standDownUntilMs.
        ObjectGuid standDownTarget;
        uint32 standDownUntilMs = 0;

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
        // Added to `expected` too: the pack-coming wait and the line of sight pull count them.
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
        // casters came (a heal's threat lands on every enemy at once). Damage dealers wait for
        // the tank's threat each on its own target (DamageTargets, ThreatRoom).
        bool holdHealing = false;
        // Settle past the arrival wait: pulled enemies coming for the tank are still on their
        // way (further than PullAssigner::ARRIVAL_DISTANCE; at most PullOpening::HOLD_MAX_MS of
        // Settle). Settle lasts while true; the area spell waits (AreaPhase). Written by
        // PullAssigner (since 2026-11-13; before, the threat opening held the party for it).
        bool packComing = false;

        // Where the pull puts the fight: a walk-in fought where the pack stands at packAt, a shot
        // or face pull where the tank opens, brought back at `spot`, a line of sight pull at
        // losPoint. False = the fight is already on (Settle past the arrival wait: the tank's
        // position). The members read TeamPlan::PlannedFightSpot: the fight spot may take the
        // pile on from here (FightSpotPlan::planned).
        bool PullFightSpot(Position3& out) const
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

        // The pull places the tank (PullBehavior walks it): going out, bringing the pack back,
        // waiting where it opened for the pack, hiding from casters. The tank's stance
        // (TankStanceBehavior) stands down then, the fight spot is not judged except for patrols
        // coming soon (FightSpotAssigner).
        bool PlacesTank() const
        {
            switch (phase)
            {
                case PullPhase::Approach:
                case PullPhase::Return:
                    return true;
                case PullPhase::Settle:
                    return awaitArrival || losBreak == PullLosBreak::Active;
                default:
                    return false;
            }
        }

        // Members other than the tank that are still out of combat wait while true (PullBehavior
        // holds them; healers and ranged first take their places for the planned fight,
        // PlannedFightSpot): the tank goes out or brings the pack back, the pack has not arrived
        // yet, or (healers) the casters of a line of sight pull have not come.
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

    // `.partybot ai retreat` (RetreatPlanner). The player starts it: by the command, or by
    // running away himself (RunAwayAssigner, user 2026-11-15: the bots read the player); bots
    // never run on their own, that would leave the player alone (combat scenario 11, T+18s).
    struct RetreatPlan
    {
        bool active = false;
        Position3 point;                    // rally point, out of every enemy's threat area (outdoors)
        // In a dungeon (creatures never give up there): out through the door along the way the
        // leader came in (Perception/Spatial/WayIn.h), the door end first. Every bot walks it
        // back from where it joins it; at the door it steps out (RetreatPlanner::Step).
        bool toDoor = false;
        Way doorWay;                        // point = its first point (the door end)
        bool declineNoted = false;          // "we cannot retreat" said this fight
        // RunAwayAssigner: the leader runs away, start a retreat (the map step RetreatPlanner::Step
        // does, it needs the map). `automatic`: this retreat was started that way.
        bool requested = false;
        std::string requestWhy;
        bool automatic = false;
        uint32 declinedAtMs = 0;            // the last request that could not start (no retry for a while)
        uint32 startedMs = 0;
        uint32 enemiesAtStart = 0;
        RetreatOutcome lastOutcome = RetreatOutcome::None;
        std::vector<PullMemberName> names;  // for notes written by the pure assigner

        // "Consider retreating" hint (RetreatAssigner::Advise): at most once per fight.
        bool adviceSent = false;
        uint32 dangerSinceMs = 0;           // 0 = not in danger now
    };

    // Where the main tank keeps the pile (FightSpotAssigner, the spot from
    // Coordination/Situation/FightSpot/FightSpot.h; tank fight spot doc
    // .kiro/docs/2026-11-14-tank-fight-spot.md): the tank's place loses more than a field cell
    // around it (a patrol walking in, a control that breaks on damage next to the pile). The
    // tank walks to the first spot that passes the live ground / idle creature / path checks
    // (TankStanceBehavior); the enemies follow the tank. The others pick their own cells around
    // the tank's new place (the stance modules). Not moving: the tank holds where it stands.
    struct FightSpotPlan
    {
        bool moving = false;
        ObjectGuid tank;
        ObjectGuid patrol;                  // the patrol costing the most at the start, empty = none
        Position3 from;                     // the tank's position at decision time
        std::vector<Position3> spots;       // best first, fixed while moving
        float benefit = 0.0f;               // points not lost there (FightSpot: here - best spot's loss)
        float cost = 0.0f;                  // points lost walking there
        std::string why;                    // FightSpot::Choice::why
        uint32 sinceMs = 0;
        // Written by the tank's TankStanceBehavior (same exception as PullPlan::rangedShot):
        // index of the spot that passed the live checks, -1 = none yet, -2 = none of them passed.
        mutable int32 chosen = -1;
        mutable Position3 chosenSpot;       // spots[chosen] with its ground height
        // What the leader was told last while not moving (said again only when it changes).
        std::string lastNote;

        // Before the fight (tank fight spot doc step 2): a pull fought "here" (walk-in, shot or
        // face pull, not brought back, not a corner) is judged once for its pull spot
        // (PullPlan::PullFightSpot) with the pull's fight estimate. A better cell: `planned`, the
        // members take their places for plannedSpots.front() (TeamPlan::PlannedFightSpot), and
        // once the pull no longer places the tank the move starts with these spots.
        bool planned = false;
        bool plannedJudged = false;         // judged for plannedFor / plannedPullMs (planned or not)
        Position3 plannedFor;               // the pull spot it was judged for
        uint32 plannedPullMs = 0;           // PullPlan::plannedAtMs it was judged for
        std::vector<Position3> plannedSpots;
        float plannedBenefit = 0.0f;
        float plannedCost = 0.0f;
        std::string plannedWhy;

        void ClearPlanned()
        {
            planned = false;
            plannedJudged = false;
            plannedSpots.clear();
            plannedWhy.clear();
        }

        bool Moves(ObjectGuid bot) const { return moving && !bot.IsEmpty() && bot == tank; }
        // Where the pile will be: the chosen spot (the first one before the tank checked them),
        // else false (not moving).
        bool Target(Position3& out) const
        {
            if (!moving || spots.empty())
                return false;
            out = chosen >= 0 ? chosenSpot : spots.front();
            return true;
        }
    };

    // A lure in the fight: the patrol guard's (PatrolGuardPlan::lures) or a caster standing
    // back anywhere (CasterLurePlan).
    enum class LosLureState : uint8
    {
        Search,     // the map step (PatrolPlanner / CasterLurePlanner) looks for a point
        Active,     // the hider stands at `point` until the casters came (the assigner)
        NoPoint,    // no point found
        Done,       // the casters came out (or died, or turned to someone else)
        Failed,     // they did not come within the assigner's timeout
    };

    // Casters standing back, casting at `hider` (their victim, a bot): the hider steps out of
    // their sight (LosBreak.h) and keeps fighting what it can reach there, so they walk to it
    // (LureBehavior).
    struct LosLure
    {
        ObjectGuid hider;
        std::vector<ObjectGuid> casters;
        LosLureState state = LosLureState::Search;
        Position3 point;
        uint32 sinceMs = 0;                 // Active since then
    };

    // Guarding against a patrol (PatrolAssigner): it walks over enemies standing away from the
    // pile, not into the pile itself (that is the fight spot's: FightSpotPlan). Nobody goes
    // over to them until it walked past.
    struct PatrolGuardPlan
    {
        bool active = false;
        ObjectGuid patrol;
        ObjectGuid tank;
        uint32 sinceMs = 0;
        // What the patrol costs if the fight goes to one of `avoid` (SpotCost::PatrolJoinPoints
        // at the worst of them): the worth of killing a caster no lure brings out.
        float points = 0.0f;
        // Patrols guarded until GUARD_TIMEOUT_MS this fight: not guarded again.
        std::vector<ObjectGuid> timedOut;
        // Patrols the leader was told about this fight (once each).
        std::vector<ObjectGuid> announced;

        // Enemies standing where the patrol walks before the fight ends, every refresh while
        // active. Nobody chases them (TeamBotAI::BeginChasing), the tank does not walk over
        // to win them back and nobody is given them as a target (TargetAssigner), unless a bot
        // was given the enemy to kill (`kill`). Melee enemies walk out by themselves.
        std::vector<ObjectGuid> avoid;
        // Lures: written by PatrolAssigner, the point by PatrolPlanner::Step.
        std::vector<LosLure> lures;
        // Casters of `avoid` no lure brings out (no point, timed out, victim not a bot): a
        // ranged damage dealer kills each (FocusAssigner intercept; since 2026-11-03 it runs after PatrolAssigner and reads this in the same refresh).
        std::vector<ObjectGuid> kill;

        bool Avoids(ObjectGuid enemy) const
        {
            return active && std::find(avoid.begin(), avoid.end(), enemy) != avoid.end();
        }

        // The lure `bot` hides for, still going (Active), or null.
        LosLure const* ActiveLureOf(ObjectGuid bot) const
        {
            if (!active)
                return nullptr;
            for (LosLure const& l : lures)
                if (l.hider == bot && l.state == LosLureState::Active)
                    return &l;
            return nullptr;
        }
    };

    // Casters standing back after the pull (Coordination/Situation/Los/CasterLureAssigner.h):
    // their victim steps out of their sight so they walk over. The pull's own break is
    // PullPlan::losBreak, a patrol's path PatrolGuardPlan::lures.
    struct CasterLurePlan
    {
        std::vector<LosLure> lures;             // this fight's, ended ones kept (one per caster)

        LosLure const* ActiveLureOf(ObjectGuid bot) const
        {
            for (LosLure const& l : lures)
                if (l.hider == bot && l.state == LosLureState::Active)
                    return &l;
            return nullptr;
        }
        // Still going (Search or Active) for `caster`.
        bool Lures(ObjectGuid caster) const
        {
            for (LosLure const& l : lures)
                if ((l.state == LosLureState::Search || l.state == LosLureState::Active) &&
                    std::find(l.casters.begin(), l.casters.end(), caster) != l.casters.end())
                    return true;
            return false;
        }
    };

    // The pull's patrol hold before the patrol comes (Coordination/Situation/Patrol/PatrolHold.h,
    // battlefield groups doc 7): the controller walks to a spot in time and holds it outside
    // the pile. Written every refresh while the held patrol is idle; the doctrine's hold
    // (DoctrinePlan::holds, planned) takes over once it is in the fight.
    struct PatrolHoldPlan
    {
        bool active = false;
        ObjectGuid patrol;
        ObjectGuid bot;
        uint32 spellId = 0;
        Position3 fightAt;              // the pile: the main tank, before the fight the planned spot
        float clearYd = 0.0f;           // ControlValue::ClearOfPileYd: held at least this far from fightAt
        Position3 holdAt;               // the last track point before it gets within clearYd
        float holdSec = 0.0f;           // when it is there, from the refresh
        std::vector<Position3> spots;   // where the controller stands, best first (sight is checked live)
        uint32 goAtMs = 0;              // WorldTimer ms the controller sets out (0 = at once)
        std::string why;
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
        bool manaCritical = false;   // a mana leech takes mana a healer needs (DispelValue::Worth): urgent
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
        bool task = false;           // a doctrine hold (STAMP_MUST_CONTROL): Urgent, not valued against its cost
    };

    // How one loose enemy is taken off its victim, and by whom (RescueAssigner).
    enum class RescueWay : uint8
    {
        Taunt,          // the tank taunts it (spellId), then holds it (TargetAssigner gives it the enemy)
        Threat,         // the tank walks over and wins it back with threat (its target)
        ThreatDrop,     // the victim drops its threat (Fade, Feint, Cower, Feign Death, Vanish: spellId)
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
        // Taunt: wait until then (WorldTimer ms; 0 = taunt now, RescueAssigner::TauntWaitSec):
        // the enemy runs at a ranged victim who keeps adding threat the taunt copies. The tank
        // keeps it in reach meanwhile (TankTargets), RescueBehavior taunts earlier when it is
        // about to reach its victim or leave the reach, the victim keeps casting (EscapeBehavior).
        uint32 waitUntilMs = 0;
        float waitGain = 0.0f;       // threat the wait is expected to copy on top (display)
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
        // Sprint / Dash on itself (FleeStopAbility::speedPct): cast while it runs and the bot is
        // out of melee reach of it.
        bool catchUp = false;
    };

    // A cooldown for a party member (BoostAssigner, cooldown doc 2026-11-14): `bot` casts
    // `spellId` on `target` (Mana Tide Totem: on itself). BoostBehavior casts it.
    struct BoostDuty
    {
        ObjectGuid bot;
        ObjectGuid target;
        uint32 spellId = 0;
        float points = 0.0f;         // net worth (Perception/Values/BoostValue.h)
    };

    // A resurrection in combat (ResurrectAssigner, user 2026-11-15): `bot` casts `spellId`
    // (Rebirth) on the dead member `target`. ResurrectBehavior casts it.
    struct ResurrectDuty
    {
        ObjectGuid bot;
        ObjectGuid target;
        uint32 spellId = 0;
        float points = 0.0f;         // net worth (Perception/Values/ResurrectValue.h)
    };

    // Only controlled enemies are left (ControlledEndAssigner): the tank wakes the first one
    // (attacks it, TeamBotAI::IsAssignedToBreakControl), after waiting for mana only when the
    // fight would be too hard now.
    struct ControlEndPlan
    {
        bool active = false;
        ObjectGuid enemy;            // the one to wake first (its control ends soonest)
        ObjectGuid breaker;          // the main tank
        uint32 breakAtMs = 0;        // wake it from then on (now = no wait)
        bool noted = false;          // the leader was told
        // Why (the tank's target reason): only controlled enemies left, or the doctrine's area
        // pile has one of ours held by a bot's control (ControlledEnd.h).
        char const* why = "only controlled enemies left: wake one";

        bool Breaks(ObjectGuid bot, ObjectGuid target, uint32 nowMs) const
        {
            return active && bot == breaker && target == enemy && int32(nowMs - breakAtMs) >= 0;
        }
    };

    // The area phase (Coordination/Doctrine/AreaPhase.h, .kiro/docs/2026-11-08-aoe-phase.md): the area
    // damage spells the party goes for now, who casts each, where from, and what is done first.
    // A reservation for each caster: AreaDamageBehavior walks, shields and casts in that order.
    enum class AreaStep : uint8
    {
        Move,       // walk to `spot` (farther from the pack: the enemies it turns take longer to come)
        Shield,     // put `shieldSpellId` on itself first
        Cast,
        Channel,    // running: the brain only keeps its value (endCost, the priest's shield)
    };

    enum class AreaWait : uint8
    {
        None,
        Tank,       // the tank tops up the enemies the spell would turn
        Shield,     // the caster's shield comes off cooldown
    };

    // One caster's area spell in the phase (AreaPhase.h): which spell, where, from where, what first.
    struct AreaCast
    {
        AreaStep step = AreaStep::Cast;
        ObjectGuid caster;
        uint32 spellId = 0;
        // Ground spell: the enemy the centre is placed by (AreaPlace.h: the pile's anchor or the
        // first runner); empty = around the caster.
        ObjectGuid centre;
        Position3 centrePos;         // ground spell: the point it is cast at (BotAction::atPoint)
        Position3 spot;              // Move: where to cast from (spots[0])
        // Move: the best cells first (AreaSpot.h); the cerebellum takes the first that passes
        // its sight and path checks.
        std::vector<Position3> spots;
        float moveBenefit = 0.0f;    // what casting there gains over here (before the walk)
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
        // What ending this spell now loses (a tank sent elsewhere: Stragglers pays it; the fight
        // spot values the channel with the pile moved itself, FightSpot.h).
        float endCost = 0.0f;
        // The spell counts on the tanks' taunts for the enemies it turns: what it loses
        // without them (RescueAssigner adds it to a taunt spent on another enemy).
        float tauntReserve = 0.0f;
        std::vector<ObjectGuid> targets;
        uint32 sinceMs = 0;

        bool Targets(ObjectGuid enemy) const
        {
            return std::find(targets.begin(), targets.end(), enemy) != targets.end();
        }
    };

    // A caster whose best spell is worth more a little later (AreaPhase step 3): the tank first
    // gives the enemies it would turn a runway through it, or its shield is off cooldown first.
    // gain = later - now (points).
    struct AreaWaitPlan
    {
        AreaWait wait = AreaWait::None;
        ObjectGuid caster;
        float sec = 0.0f;
        float gain = 0.0f;
        float shortfall = 0.0f;      // Tank: the threat the tank lacks on them in total
        uint32 sinceMs = 0;
        uint32 progressMs = 0;       // Tank: last time shortfall shrank (AreaPhase::TANK_STALL_MS)
    };

    // A caster getting ready while it has no spell of the phase yet (the pull holds the party,
    // or it waits; AreaPhase, doc .kiro/docs/2026-11-11-aoe-threat.md 4.4, 4.5, 8): it walks to
    // its cast spot (spots, best first; centre) and puts its shield up (shieldId, by the
    // opening's estimate); the ranged stance leaves it alone.
    struct AreaPrep
    {
        ObjectGuid caster;
        uint32 shieldId = 0;
        float gain = 0.0f;           // the shield's, net of its mana
        std::vector<Position3> spots;
        Position3 centre;
        float moveGain = 0.0f;       // the spot's over here, before the walk
        float moveCost = 0.0f;       // the walk (in combat; nothing is lost before the fight)
    };

    // A reservation (AreaPhase.h "Reservation"): a caster the phase is about to use. It starts
    // no spell with a cast or channel time but its area spells meanwhile (TeamPlan::
    // ForbidsLongCast). atMs = the last refresh that named it.
    struct AreaReservation
    {
        ObjectGuid caster;
        uint32 atMs = 0;
    };

    // The area phase of the party: every area damage dealer bot has its own part (a spell
    // going, a wait, the prep, a reservation, its room); several may cast at once (user
    // 2026-11-15: two mages, only one ever cast Blizzard and put its Ice Barrier up first).
    struct AreaPlan
    {
        std::vector<AreaCast> casts;
        std::vector<AreaWaitPlan> waits;
        std::vector<AreaPrep> preps;
        std::vector<AreaReservation> reserved;
        // The area casters that keep their own room (AreaPhase.h "room"; doc
        // .kiro/docs/2026-11-12-mage-hold.md 3.3.1, 3.4): AreaRoomBehavior decides when it Frost
        // Novas the enemies running at it and goes away or to the tank; EscapeBehavior only puts
        // up its danger shield.
        std::vector<ObjectGuid> roomCasters;

        bool Active() const { return !casts.empty(); }

        AreaCast const* CastOf(ObjectGuid bot) const
        {
            for (AreaCast const& c : casts)
                if (c.caster == bot)
                    return &c;
            return nullptr;
        }
        AreaWaitPlan const* WaitOf(ObjectGuid bot) const
        {
            for (AreaWaitPlan const& w : waits)
                if (w.caster == bot)
                    return &w;
            return nullptr;
        }
        AreaPrep const* PrepOf(ObjectGuid bot) const
        {
            for (AreaPrep const& p : preps)
                if (p.caster == bot)
                    return &p;
            return nullptr;
        }
        AreaReservation const* ReservationOf(ObjectGuid bot) const
        {
            for (AreaReservation const& r : reserved)
                if (r.caster == bot)
                    return &r;
            return nullptr;
        }

        bool CastBy(ObjectGuid bot) const { return CastOf(bot) != nullptr; }
        bool Reserves(ObjectGuid bot) const { return !bot.IsEmpty() && ReservationOf(bot) != nullptr; }
        bool KeepsRoom(ObjectGuid bot) const
        {
            return std::find(roomCasters.begin(), roomCasters.end(), bot) != roomCasters.end();
        }
        // A caster of the phase or one getting ready (its place is the area phase's).
        bool IsCaster(ObjectGuid bot) const { return CastBy(bot) || PrepOf(bot) != nullptr; }

        // The stance modules leave `bot`'s place to the area phase (AreaSpot.h).
        bool PlacesCaster(ObjectGuid bot) const
        {
            AreaPrep const* p = PrepOf(bot);
            return CastBy(bot) || (p && !p->spots.empty());
        }

        bool Targets(ObjectGuid enemy) const
        {
            return std::any_of(casts.begin(), casts.end(), [enemy](AreaCast const& c) { return c.Targets(enemy); });
        }

        // What ending the phase now loses: every spell's.
        float EndCost() const
        {
            float sum = 0.0f;
            for (AreaCast const& c : casts)
                sum += c.endCost;
            return sum;
        }

        // The taunts the spells that do not hit `enemy` count on (a taunt spent on it pays them).
        float TauntReserveOutside(ObjectGuid enemy) const
        {
            float sum = 0.0f;
            for (AreaCast const& c : casts)
                if (!c.Targets(enemy))
                    sum += c.tauntReserve;
            return sum;
        }
    };

    // ---- Doctrine (brain layer 2, Coordination/Doctrine/TacticalDoctrine.h; doc
    // .kiro/docs/2026-11-09-doctrine section 4): how this fight is fought, and the stamps on
    // the enemies the duties read.
    enum class FightPhase : uint8
    {
        None,       // no fight of ours
        Planned,    // a pull is planned, nobody fights yet: only the groups (DoctrinePlan::groups)
        Gathering,  // the pull still runs (the pack still coming): the pile tactic is chosen every refresh
        Clash,
        Cleanup,    // at most CLEANUP_FREE enemies not under a control
    };

    enum class PileTactic : uint8
    {
        None,       // not chosen (no fight, no main tank, a retreat)
        Area,       // the pile around the tank is left to area spells: no control that breaks on damage there
        Pick,       // the pile may be controlled, killed one by one
    };

    // Bits of DoctrineEnemy::stamps (32 bits: the 16 of uint16 were all used, battlefield
    // groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md 9).
    enum DoctrineStamp : uint32
    {
        STAMP_IN_CLUSTER    = 1 << 0,   // the pile around the main tank (or a melee enemy on its way there)
        STAMP_OUTLIER       = 1 << 1,   // stands back away from the pile (casters)
        STAMP_SPECIAL       = 1 << 2,   // a heavy enemy not at the pile yet (a patrol walking in)
        STAMP_NO_MEZ        = 1 << 3,   // no control that breaks on damage, no fear (CrowdControlAssigner)
        STAMP_COMMITTED     = 1 << 4,   // held by a bot's control: that bot renews it, does not move it
        STAMP_PLAYER_TARGET = 1 << 5,   // a player hits it
        // Its victim steps out of its sight so it walks over (OutlierTactic::Lure; the pull's
        // map step SearchLosBreak looks for the point, after the pull CasterLurePlanner).
        STAMP_LURE          = 1 << 6,
        // Roles and sources (todo 9, .kiro/docs/2026-11-09-doctrine/2026-11-10-roles-sources.md):
        STAMP_SELF_DESTRUCT = 1 << 7,   // blows itself up (Broken Cadaver): no taunt, no rescue, no control
        STAMP_KILL_NOW      = 1 << 8,   // a self-destruct the party kills before its earliest blast: the focus
        STAMP_STAND_CLEAR   = 1 << 9,   // a self-destruct that will blow first: everyone but its victim steps out
        STAMP_SWARM         = 1 << 10,  // from a trap (crate insects): no control, no tank threat risk, the area spells'
        STAMP_ALARM         = 1 << 11,  // brings more as it aggroes: display (its summons are in FightLoad)
        STAMP_NO_TAUNT      = 1 << 12,  // no single target taunt (swarm, self-destruct, taunt immune, totem): RescueAssigner
        STAMP_KILL_ORDER    = 1 << 13,  // killed first (healer, summoner, totem, mana drain, resurrects): the focus is among these
        STAMP_HOLD_FIRE     = 1 << 14,  // a damage shield / spell reflection is on it: nobody hits it (EngageBlock, Constraints)
        STAMP_FREE_ALLY     = 1 << 15,  // it mind controls a party member: the focus, killed at once (TargetAssigner)
        // Stragglers (Coordination/Doctrine/Stragglers.h, battlefield groups doc 3, user 2026-11-13):
        STAMP_STRAGGLER     = 1 << 16,  // a melee straggler on its way: the tank's, damage dealers leave it (TargetAssigner, DamageTargets)
        STAMP_DPS_ONLY      = 1 << 17,  // standing back, hurts its victim less than the tank's going costs: no rescue (RescueAssigner)
        // The fight is not bearable without holding it (Coordination/Doctrine/TacticCheck.h,
        // DoctrinePlan::holds): a task for its controller, nobody else controls it (CrowdControlAssigner).
        STAMP_MUST_CONTROL  = 1 << 18,
        // The tank does not need to hold it (user 2026-11-14: fodder, a trap swarm; not a
        // self-destruct, its own stamps decide): damage dealers hit it without waiting for the
        // tank's threat (DamageTargets::ThreatAllows, ThreatBehavior's stop).
        STAMP_NO_HOLD       = 1 << 19,
        // Area pile (user 2026-11-15, strat-20261115-134307): held by a bot's control that breaks
        // on damage, in the pile under the Area tactic. The area spells (and splash, chained hits)
        // go on as if it were awake: hit, it wakes and joins the pile; missed, it sleeps until
        // only controlled enemies are left (ControlledEnd.h). Single target damage still leaves
        // it alone (TeamPlan::AreaKeepsUnbroken).
        STAMP_AREA_MAY_WAKE = 1 << 20,
        // Held by an off-tank (Coordination/Doctrine/OffTank.h, DoctrinePlan::offTanks, user
        // 2026-11-15): not loose while on it (no rescue, the main tank leaves it), damage dealers
        // kill it last and leave room under the off-tank's threat, no control on it.
        STAMP_OFF_TANK      = 1 << 21,
    };

    // An enemy an off-tank holds for the party (OffTank.h): kept until the enemy dies, the
    // holder cannot hold it (dead, cannot act, about to die) or lost it for a while.
    struct OffTankHold
    {
        ObjectGuid enemy;
        ObjectGuid holder;
        uint32 sinceMs = 0;         // held from (world refresh time)
        uint32 seenOnMs = 0;        // the enemy was last hitting the holder at
        // Handed over from the main tank (OffTank.h "Handed"): the holder is winning it with
        // threat until then (WorldTimer ms); 0 = it has it.
        uint32 takeByMs = 0;
        float points = 0.0f;        // handed over: what it saves (FightLoad::Loss as is - held)
        std::string why;
    };

    // An enemy the doctrine holds under a control so the fight is bearable (TacticCheck.h).
    struct DoctrineHold
    {
        ObjectGuid enemy;
        ObjectGuid bot;
        uint32 spellId = 0;
        bool planned = false;       // the pull's choice for a patrol (PullPlan::holdPatrol), not the fight's check
        ControlKind kind = ControlKind::Mez;
        float points = 0.0f;        // what holding it saves (FightLoad::Loss as is - held)
        std::string why;
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
        // Gathering, no tactic, Pick (the tank follows the threat risk, TankTargets; until
        // 2026-11-13 Pick had Focus: the tank hit the party focus too).
        None,
        HoldPile,   // Area: the tank keeps the pile (threat risk) and protects the area caster: a rescue
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

    // The kinds of groups the battlefield is split into (Coordination/Doctrine/Battlefield.h,
    // doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md 3).
    enum class GroupKind : uint8
    {
        Pile,               // melee enemies together where the tank fights (with a heavy one: boss + adds)
        MeleeStraggler,     // melee enemies away from the pile: they walk to it
        RangedStraggler,    // enemies standing back (casters, archers)
        Patrol,             // one walking patrol (planned: predicted to come; or it joined the fight)
        Reinforcement,      // death spawns, summons, the called, trap swarms (they come in the fight)
    };

    // One group, kept by guid from the pull's plan to the end of the fight (Battlefield.h).
    struct DoctrineGroup
    {
        GroupKind kind = GroupKind::Pile;
        std::vector<ObjectGuid> members;
        // Members of a group other than the pile that came within CLUSTER_YD of the main tank:
        // they are the pile's from then on (one way, Battlefield::StampOf).
        std::vector<ObjectGuid> joined;
        ObjectGuid heavy;               // Pile: its heavy enemy (boss + adds), empty = none
        float arriveSec = -1.0f;        // Patrol planned before the pull: comes this long after the plan, -1 = here
        bool planned = false;           // made from the pull's plan (else from an enemy that showed up)
        // Patrol: held outside the pile (PullPlan::holdPatrol, TacticCheck.h): it never joins the
        // pile, its stamp stays Special (Battlefield::StampOf).
        bool holdOut = false;
        // An unplanned add batch (Battlefield.h "adds"): creatures standing idle that came in
        // together (bad standing, a stray hit, a runner's call); its melee and its standing back
        // ones are two groups with the same number. 0 = not an add batch.
        uint32 addBatch = 0;
        uint32 sinceMs = 0;

        bool Has(ObjectGuid g) const { return std::find(members.begin(), members.end(), g) != members.end(); }
        bool Joined(ObjectGuid g) const { return std::find(joined.begin(), joined.end(), g) != joined.end(); }
    };

    struct DoctrineEnemy
    {
        ObjectGuid guid;
        int8 group = -1;            // index into DoctrinePlan::groups, -1 = none (no main tank)
        uint32 stamps = 0;
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
        std::vector<DoctrineGroup> groups;  // the battlefield split (Battlefield.h), kept by guid
        // Controls the fight needs (TacticCheck.h), kept until the enemy dies, the controller
        // cannot act or the hold fails; added to at the reselect events.
        std::vector<DoctrineHold> holds;
        std::string holdsWhy;               // the last check: "bearable" / "held 1, bearable" / "not bearable, no control left"

        DoctrineHold const* HoldOn(ObjectGuid enemy) const
        {
            for (DoctrineHold const& h : holds)
                if (h.enemy == enemy)
                    return &h;
            return nullptr;
        }
        DoctrineHold const* HoldBy(ObjectGuid bot) const
        {
            for (DoctrineHold const& h : holds)
                if (h.bot == bot)
                    return &h;
            return nullptr;
        }
        // Enemies off-tanks hold (OffTank.h), kept like the holds.
        std::vector<OffTankHold> offTanks;
        OffTankHold const* OffTankOf(ObjectGuid enemy) const
        {
            for (OffTankHold const& h : offTanks)
                if (h.enemy == enemy)
                    return &h;
            return nullptr;
        }
        // `holder` holds `enemy` as an off-tank: a holder for that enemy (damage dealers leave
        // room under its threat, it is not loose).
        bool OffTankHolds(ObjectGuid holder, ObjectGuid enemy) const
        {
            OffTankHold const* h = OffTankOf(enemy);
            return h && h->holder == holder;
        }
        // The enemy `holder` holds as an off-tank, or empty.
        ObjectGuid OffTankEnemyOf(ObjectGuid holder) const
        {
            for (OffTankHold const& h : offTanks)
                if (h.holder == holder)
                    return h.enemy;
            return ObjectGuid();
        }
        std::vector<DoctrineEnemy> enemies;
        // The area spell the Area tactic counts on (Coordination/Doctrine/AreaIntent.h): each
        // area dealer bot's threat per second with it, on the pile, and the spell's length. The
        // tank's threat risk is judged with it (ThreatRisk: the tank's target, its area threat;
        // AreaPhase's wait for the tank). Empty / 0 under another tactic.
        std::vector<PlannedThreat> areaThreat;
        float areaSec = 0.0f;

        DoctrineEnemy const* Find(ObjectGuid enemy) const
        {
            for (DoctrineEnemy const& d : enemies)
                if (d.guid == enemy)
                    return &d;
            return nullptr;
        }
        bool Has(ObjectGuid enemy, uint32 stamp) const
        {
            DoctrineEnemy const* d = Find(enemy);
            return d && (d->stamps & stamp) != 0;
        }
        // The planned area output on `enemy`: `areaThreat` on the pile, nothing elsewhere.
        std::vector<PlannedThreat> const& AreaThreatOn(ObjectGuid enemy) const
        {
            static std::vector<PlannedThreat> const none;
            return Has(enemy, STAMP_IN_CLUSTER) ? areaThreat : none;
        }
        // How far ahead the tank's threat risk is judged: the area spell, else
        // ThreatRunway::HORIZON_SEC.
        float RunwayHorizon() const { return areaSec > 0.0f ? areaSec : ThreatRunway::HORIZON_SEC; }
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

    // A member's TeamFunction bits (CommanderTypes.h) for the team.
    struct MemberCover
    {
        ObjectGuid member;
        uint8 functions = 0;        // TeamFunction bits: its role's own plus `added`
        uint8 added = 0;            // TeamFunction bits given because nobody else had them
        char const* reason = "";    // static string, why `added`
    };

    // Who covers which function (RoleAssigner, between fights, kept during a fight). Every
    // member gets its role's functions; a function nobody has is added to the bots that can
    // do it. A full party has no gap: the table then says the same as the roles.
    struct CoveragePlan
    {
        std::vector<MemberCover> members;   // non-pet members, players too
        // Nobody holds (no tank): each member takes the enemies that hit it.
        bool noHolder = false;

        MemberCover const* Find(ObjectGuid member) const
        {
            for (MemberCover const& c : members)
                if (c.member == member)
                    return &c;
            return nullptr;
        }
        uint8 Of(ObjectGuid member) const
        {
            MemberCover const* c = Find(member);
            return c ? c->functions : 0;
        }
        bool Has(ObjectGuid member, TeamFunction function) const { return (Of(member) & function) != 0; }
    };

    // How much a healer bot may deal damage (Coordination/Doctrine/HealerDamage.h; doc
    // .kiro/docs/2026-11-21-healer-damage-tiers.md section 3).
    enum HealerDamageTier : uint8
    {
        HEALER_DAMAGE_NONE  = 0,    // heals only
        HEALER_DAMAGE_FREE  = 1,    // free attacks: wand, melee swings in the open world
        HEALER_DAMAGE_SPARE = 2,    // + damage spells while the mana is high
        HEALER_DAMAGE_FULL  = 3,    // a hybrid: damage down to a mana reserve
        HEALER_DAMAGE_TIERS = 4
    };

    struct HealerDamageDuty
    {
        ObjectGuid bot;
        uint8 tier = HEALER_DAMAGE_NONE;    // HealerDamageTier
        bool noReserve = false;             // FULL without the mana reserve (HealerDamage.h)
        // Fights in melee (HealerDamage.h): walks to its target and swings (MeleeStanceBehavior),
        // instead of standing back with the healer stance.
        bool melee = false;
        char const* reason = "";            // static string
    };

    struct HealerDamagePlan
    {
        std::vector<HealerDamageDuty> healers;  // healer-role bots only
        // Raising a tier waits for a change in the fight: the acting enemies of the last
        // refresh (count), and when the open world forecast was last made.
        uint32 enemyCount = 0;
        uint32 forecastMs = 0;
        bool hard = false;                      // that forecast (FightPlace::IsHard)

        HealerDamageDuty const* Find(ObjectGuid bot) const
        {
            for (HealerDamageDuty const& d : healers)
                if (d.bot == bot)
                    return &d;
            return nullptr;
        }
        bool Melee(ObjectGuid bot) const
        {
            HealerDamageDuty const* d = Find(bot);
            return d && d->melee;
        }
    };

    // Paladin blessings and party auras: who gives which (BlessingAssigner.h). The paladin
    // rotation casts them out of combat.
    struct BlessingDuty
    {
        ObjectGuid bot;
        ObjectGuid target;
        uint32 spellId = 0;
    };

    struct PaladinAuraDuty
    {
        ObjectGuid bot;
        uint32 spellId = 0;
    };

    struct BlessingPlan
    {
        bool active = false;    // the module ran (off: the rotation picks on its own)
        std::vector<BlessingDuty> blessings;
        std::vector<PaladinAuraDuty> auras;

        // 0 = nothing for this pair.
        uint32 BlessingFor(ObjectGuid bot, ObjectGuid target) const
        {
            for (BlessingDuty const& d : blessings)
                if (d.bot == bot && d.target == target)
                    return d.spellId;
            return 0;
        }

        uint32 AuraFor(ObjectGuid bot) const
        {
            for (PaladinAuraDuty const& d : auras)
                if (d.bot == bot)
                    return d.spellId;
            return 0;
        }
    };

    // Shaman totems: which one each shaman bot keeps up per element (TotemAssigner.h).
    // ShamanRotation drops them in combat.
    struct TotemDuty
    {
        ObjectGuid bot;
        uint32 spellIds[MAX_TOTEM_SLOT] = {};   // by TotemSlot, 0 = none in that element
    };

    struct TotemPlan
    {
        bool active = false;    // the module ran (off: the rotation picks on its own)
        std::vector<TotemDuty> duties;

        // 0 = nothing in that element.
        uint32 TotemFor(ObjectGuid bot, TotemSlot slot) const
        {
            for (TotemDuty const& d : duties)
                if (d.bot == bot)
                    return d.spellIds[slot];
            return 0;
        }
    };

    // Warlock damage curses: which one each warlock bot keeps on its victim (CurseAssigner.h).
    // CurseBehavior casts it when CurseValue says it pays.
    struct CurseDuty
    {
        ObjectGuid bot;
        uint32 spellId = 0;
        uint8 kind = 0;         // CurseKind
    };

    struct CursePlan
    {
        bool active = false;    // the module ran (off: no curses)
        std::vector<CurseDuty> duties;

        CurseDuty const* For(ObjectGuid bot) const
        {
            for (CurseDuty const& d : duties)
                if (d.bot == bot)
                    return &d;
            return nullptr;
        }
    };

    // The judgement each paladin bot keeps on its victim (JudgementAssigner.h): the seal it
    // judges for it. PaladinRotation casts it.
    struct JudgementDuty
    {
        ObjectGuid bot;
        uint32 sealSpellId = 0;
    };

    struct JudgementPlan
    {
        bool active = false;    // the module ran (off: no judgement kept up)
        std::vector<JudgementDuty> duties;

        uint32 SealFor(ObjectGuid bot) const
        {
            for (JudgementDuty const& d : duties)
                if (d.bot == bot)
                    return d.sealSpellId;
            return 0;
        }
    };

    // ---- Contingencies (Coordination/Situation/Crisis/CrisisAssigner.h, user 2026-11-15): the
    // tank or the healers are gone in a fight, and who could carry it.
    enum CrisisLoss : uint8
    {
        CRISIS_TANK   = 1 << 0,     // every tank is dead
        CRISIS_HEALER = 1 << 1,     // every healer is dead or out of mana
    };

    // One way to go on: a stand-in tank (tank loss) and who heals (healer loss), with the fight
    // forecast for it (FightLoad::Lineup).
    struct CrisisOption
    {
        ObjectGuid standIn;             // empty: the tank as it is (healer loss only)
        StandInWay way = StandInWay::None;
        std::vector<ObjectGuid> healers;    // spare healers healing too (PartyMemberFacts::spareHealRates)
        bool bearable = false;          // FightLoad::Bearable
        float score = 0.0f;             // FightLoad::Score
        std::string forecast;           // FightLoad::Describe
    };

    struct CrisisPlan
    {
        uint8 lost = 0;                 // CrisisLoss bits, 0 = no contingency
        uint32 sinceMs = 0;             // world refresh time `lost` became what it is
        std::vector<CrisisOption> options;
        int best = -1;                  // index into options, -1 = none can be run
        ObjectGuid lastStandIn;         // the last best option (sticky, CrisisAssigner::STICKY_SCORE)
        size_t lastHealers = 0;
        // What the last note said (loss, chosen stand-in, healers, bearable): a new note only
        // when it changes, not for every new forecast number.
        std::string notedKey;
    };

    // The fight is lost (Coordination/Situation/Crisis/WipeAssigner.h): keep the way back.
    struct WipeHider
    {
        ObjectGuid bot;
        uint32 spellId = 0;             // Feign Death, Vanish
    };

    struct WipePlan
    {
        bool active = false;            // called for the rest of this fight
        uint32 sinceMs = 0;
        uint32 losingSinceMs = 0;       // the contingency had no bearable option since (0 = it has)
        std::string why;
        std::vector<ObjectGuid> revivers;   // rise by themselves and resurrect the others
        std::vector<WipeHider> hiders;      // drop out of the fight and stay out
        // A warlock bot puts its Soulstone on a member who resurrects (no reviver yet).
        ObjectGuid stoneBy;
        ObjectGuid stoneOn;
        uint32 stoneItem = 0;
        std::string notedKey;

        bool Hides(ObjectGuid bot) const
        {
            for (WipeHider const& h : hiders)
                if (h.bot == bot)
                    return true;
            return false;
        }
    };

    // A bot given another role for the rest of the fight (CrisisAssigner: the best option's
    // stand-in tank, its spare healers). PartyCommander::ApplyEmergencyRoles sets it on the bot
    // (TeamBotAI::SetEmergencyRole) and gives the old role back once the fight is over.
    struct EmergencyRole
    {
        ObjectGuid bot;
        uint8 role = 0;                 // CombatBotRoles
        StandInWay way = StandInWay::None;  // a tank: how it holds (the note)
    };

    // Members the player told to wait where they stand ("在这儿等我", Situation/Stay.h): they do
    // not follow and are not teleported to the leader until told to follow again.
    struct StaySpot
    {
        ObjectGuid member;
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        uint32 sinceMs = 0;
    };

    struct StayPlan
    {
        std::vector<StaySpot> members;

        StaySpot const* Find(ObjectGuid member) const
        {
            for (StaySpot const& s : members)
                if (s.member == member)
                    return &s;
            return nullptr;
        }
    };

    // A warlock's Ritual of Summoning for a member far away (Situation/Summon.h): who casts, who
    // clicks the portal, who is summoned.
    struct SummonRitual
    {
        ObjectGuid warlock;
        ObjectGuid target;
        std::vector<ObjectGuid> helpers;    // two members who click the portal
        uint32 sinceMs = 0;
    };

    // Every ritual under way (one per warlock, several at once in a raid), and the gathering of
    // members left behind (Situation/GatherPlanner.h).
    struct SummonPlan
    {
        std::vector<SummonRitual> rituals;
        // Targets whose summon went out lately (not asked again while they may still accept).
        std::vector<std::pair<ObjectGuid, uint32>> sent;    // target, ms
        // The team stands still (the leader settled) since; the leader's last place.
        uint32 settledSinceMs = 0;
        uint32 settledMap = 0;
        float settledX = 0.0f, settledY = 0.0f;
        bool gatherNow = false;             // the player asked ("把掉队的都拉过来"): no settle wait
        bool shortNoted = false;            // "not enough shards" said this settle

        SummonRitual const* RitualOf(ObjectGuid member) const
        {
            for (SummonRitual const& r : rituals)
                if (r.warlock == member || r.target == member ||
                    std::find(r.helpers.begin(), r.helpers.end(), member) != r.helpers.end())
                    return &r;
            return nullptr;
        }
    };

    struct TeamPlan
    {
        uint32 version = 0;
        std::vector<RoleDuty> roles;
        CoveragePlan coverage;      // RoleAssigner, with the roles
        HealerDamagePlan healerDamage;  // HealerDamageAssigner, every refresh
        BlessingPlan blessings;
        TotemPlan totems;
        JudgementPlan judgements;
        CursePlan curses;
        std::vector<InterruptDuty> interrupts;
        // Last DutyAllocator result of all duty modules (PartyCommander::RunAssigners), kept for
        // `.partybot ai plan` (who was passed over and why). Modules read their own part above.
        std::vector<DutyResult> duties;
        // The needs as offered this refresh, before the allocation (rejected candidates
        // included): for the combat recorder (u:offers, `offers` lines). Nobody reads it to decide.
        std::vector<DutyNeed> offers;
        // The later needs the allocation weighed this refresh (DutyReserve: team cooldown
        // budget), for `.partybot ai plan`.
        std::vector<DutyReserve> reserves;
        FocusTarget focus;          // TargetAssigner (encounters may override it: EncounterHelpers::SetFocus)
        std::vector<TargetDuty> targets;
        PlayerOrders orders;
        std::vector<InterceptDuty> intercepts;
        std::vector<DispelDuty> dispels;
        std::vector<ControlDuty> controls;
        std::vector<RescueDuty> rescues;
        std::vector<FleeStopDuty> fleeStops;
        std::vector<BoostDuty> boosts;
        std::vector<ResurrectDuty> resurrections;
        // Constraint for every area spell (AreaDamageBehavior, the tank's area threat in
        // ThreatBehavior): enemies about to get a control that breaks on damage
        // (CrowdControlAssigner). Enemies already under one are kept by KeepsUnbroken from the facts.
        std::vector<ObjectGuid> protectedEnemies;
        ControlEndPlan controlEnd;
        PullPlan pull;
        PullOutcome lastPullOutcome = PullOutcome::None;    // set when a pull ends, read by ClearPlanner
        ClearPlan clear;
        HuntPlan hunt;
        PushPlan push;
        // The party bot the party follows out of combat instead of the player (Guide.h: the
        // push mode's tank). Empty = the player. Only Guide::Current says whether it leads now.
        ObjectGuid guide;
        // Who waits where the player left them (Situation/Stay.h); kept until told to follow.
        StayPlan stay;
        // A summoning ritual under way (Situation/Summon.h).
        SummonPlan summon;
        RetreatPlan retreat;
        CrisisPlan crisis;
        std::vector<EmergencyRole> emergencyRoles;  // kept until the fight is over (CrisisAssigner)
        WipePlan wipe;
        // Dead bots that use their self resurrection now, in the fight (SelfRiseAssigner;
        // CorpseRun carries it out). Cleared when the fight is over.
        std::vector<ObjectGuid> rises;
        FightSpotPlan fightSpot;
        PatrolGuardPlan patrolGuard;
        CasterLurePlan casterLure;
        PatrolHoldPlan patrolHold;
        RouteGuardPlan routeGuard;
        AreaPlan area;
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

        // The rescue of loose enemy `enemy` (whoever does it), or null.
        RescueDuty const* FindRescueOf(ObjectGuid enemy) const
        {
            for (RescueDuty const& d : rescues)
                if (!d.enemy.IsEmpty() && d.enemy == enemy)
                    return &d;
            return nullptr;
        }

        BoostDuty const* FindBoostFor(ObjectGuid bot) const
        {
            for (BoostDuty const& d : boosts)
                if (d.bot == bot)
                    return &d;
            return nullptr;
        }

        ResurrectDuty const* FindResurrectFor(ObjectGuid bot) const
        {
            for (ResurrectDuty const& d : resurrections)
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
        // The same for an area spell, a melee splash or a chained hit: the doctrine's area pile
        // lets them wake a bot's control (STAMP_AREA_MAY_WAKE).
        bool AreaKeepsUnbroken(EnemyFacts const& e) const
        {
            return KeepsUnbroken(e) && !doctrine.Has(e.guid, STAMP_AREA_MAY_WAKE);
        }

        // ---- Constraints (directive types doc, .kiro/docs/2026-11-07/2026-11-07-directive-types.md).
        // What the brain forbids. Every module reads them when it builds a proposal (to pick
        // something else), the arbiter drops a proposal that breaks one (Behaviors/Constraints.h)
        // and the class rotations ask the same queries (TeamBotAI::IsEngageForbidden,
        // IsGapCloserForbidden). The exceptions live here, not at the readers.

        // Where the tank will fight once the pack is on it, as planned, for the members to take
        // their places before (stance modules, the area phase's prep, the patrol hold, the
        // ready check): the pull's spot (PullPlan::PullFightSpot), or the cell the fight spot
        // takes the pile to from there (FightSpotPlan::planned; while the tank goes out, and
        // while it waits where it opened for the pack). False = the fight is already on.
        bool PlannedFightSpot(Position3& out) const
        {
            if (!pull.PullFightSpot(out))
                return false;
            bool const before = pull.phase == PullPhase::WaitReady || pull.phase == PullPhase::Approach ||
                (pull.phase == PullPhase::Settle && pull.awaitArrival && pull.losBreak != PullLosBreak::Active);
            if (before && fightSpot.planned && !fightSpot.plannedSpots.empty())
                out = fightSpot.plannedSpots.front();
            return true;
        }

        // The lure `bot` hides for now (Active: the patrol guard's or a caster's), or null. Its
        // stance module stands down meanwhile (LureBehavior walks it).
        LosLure const* HidingFor(ObjectGuid bot) const
        {
            if (LosLure const* l = patrolGuard.ActiveLureOf(bot))
                return l;
            return casterLure.ActiveLureOf(bot);
        }

        // `bot` was given `enemy` to kill by the brain (intercept, also a caster of
        // patrolGuard.kill). This refresh's allocation.
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
            if (patrolGuard.Avoids(enemy) && !AssignedToKill(bot, enemy))
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
        // neither (TeamBotAI::IsSpellHeld), only FleeStopBehavior. A finisher kept (Kidney
        // Shot) holds the bot's other finishers too (`finisher`: the spell needs combo
        // points), so the points stay for it.
        bool HoldsSpell(ObjectGuid bot, uint32 spellId, bool finisher) const
        {
            FleeStopDuty const* d = FindFleeStopFor(bot);
            return d && (d->spellId == spellId || (finisher && d->needsCombo));
        }

        // `bot` is the area phase's reserved caster (AreaPlan::reserved): no spell with a cast
        // or channel time but its area spells (Constraints::LongCastHeld; the class rotations
        // too, TeamBotAI::PassesPartyCastChecks), so the area spell is not held back by one.
        bool ForbidsLongCast(ObjectGuid bot) const { return area.Reserves(bot); }

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
