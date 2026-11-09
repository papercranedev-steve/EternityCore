/*
 * PartyBot Commander layer 2 (brain): opening control of a pull (crowd control doc
 * .kiro/docs/2026-11-07/2026-11-07-crowd-control.md 4.3, todo 10c; Sap: .kiro/docs/2026-11-08-sap.md).
 * Its own file so the pull planner only calls it (the pull files are split by step, PullPlanner.h).
 *
 * Choose (map thread, when the tank is about to go: PullPlanner::RecheckBeforeApproach):
 *   the fight against the expected creatures (FightLoad::Build) with and without one of them
 *   controlled from the start, for every pulled creature but the pull target (it must come to
 *   the tank) and every bot but the tank with a ready single target control that affects it
 *   (ControlValue::Affects; no fears), except a control that breaks on damage on a creature
 *   the doctrine will leave to area spells or call fodder (TacticalDoctrine::PullNoMez, since
 *   2026-11-09: the same rule as in the fight, so the opening control and the fight agree;
 *   a note names them). nearTarget = the other expected creatures when the
 *   fight is where they stand (walk-in, corner), 0 when it is elsewhere (brought back, opened
 *   from range or by a face pull: the controlled one stays at its place).
 *   Sorted by the net without a spot (ControlValue::OfPull; Sap: SapValue::Optimistic), net > 0;
 *   the first MAX_SPOT_TRIES get a spot and their final net, the best positive one is taken:
 *    - a control cast once the fight started (polymorph, shackle, ...): ShotSpot::Find (the
 *      target sees it, within the spell's range - RANGE_MARGIN_YD, out of every idle
 *      creature's aggro range, off every patrol's way for STAND_SEC, a safe walk); net =
 *      OfPull's.
 *    - Sap (ControlAbility::peacefulOnly; not on wanderers or patrols, the spot must stay next
 *      to it): SapSpot::Find (stealthed walk unnoticed, no other creature sees the spot once
 *      the Sap breaks stealth, patrols for SAP_STAND_SEC); net = SapValue::Of at that spot
 *      (hit chance, expected hold, worst case failure and the fight moved to the spot). Not
 *      worth it: a note. The walk may take its length at STEALTH_WALK_YD_PER_SEC +
 *      SAP_WALK_SLACK_MS (at least PLACE_TIMEOUT_MS).
 *   Whether to control at all is the score: an easy pull gains nothing (no danger, little
 *   damage saved) and gets none.
 *   Always sets PullControlPlan::decided (the tank waits for it).
 * Update (pure, PullAssigner on every refresh):
 *   WaitReady: forget the decision (the pull was held back: decide again next time).
 *   Approach before the fight: the controller within ARRIVED_YD of the spot -> inPlace, the
 *   Approach timers start again (the tank waited); not there after PLACE_TIMEOUT_MS or it
 *   cannot act -> dropped with a note, the tank pulls without it.
 *   Sap (beforeOpener): inPlace = cast now; the target idle with a control aura on it
 *   (PackCreature::controlSpellId) -> landed, the tank pulls; the fight started first (a miss
 *   pulls the target onto the rogue: the team rescue takes it from there), not there within
 *   placeTimeoutMs, no Sap SAP_CAST_TIMEOUT_MS after reaching the spot, or it cannot act ->
 *   dropped with a note.
 * Execution: CrowdControlBehavior walks the controller to the spot (the Sap controller
 * stealthed, Takeover::Stealth) and casts once the fight started (the need comes from
 * CrowdControlAssigner, the plan's points; Sap: before it, on its own); the tank holds its
 * opener while PullControlPlan::HoldsOpener (PullBehavior).
 */

#ifndef MANGOS_COMMANDER_PULL_CONTROL_H
#define MANGOS_COMMANDER_PULL_CONTROL_H

#include "Common.h"
#include <string>
#include <vector>

class Map;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;
    struct PullPlan;

    namespace PullControl
    {
        float const RANGE_MARGIN_YD = 3.0f;
        float const ARRIVED_YD = 2.0f;
        uint32 const PLACE_TIMEOUT_MS = 10 * 1000;
        uint32 const MAX_SPOT_TRIES = 3;
        // Options listed in the "Opening control options" note (best first).
        uint32 const MAX_NOTED_OPTIONS = 5;
        // The controller stands at its spot before the fight: the walk there (at most
        // PLACE_TIMEOUT_MS) plus the tank's way to its opener. No patrol may pass it meanwhile;
        // once the fight runs, a patrol joining is the pull plan's (PullPlanner patrol check).
        float const STAND_SEC = PLACE_TIMEOUT_MS / 1000.0f + 5.0f;
        // Sap: stealth slows the rogue (guess, untested); the longer walk and the tank's way
        // after the Sap landed.
        float const STEALTH_WALK_YD_PER_SEC = 3.5f;
        uint32 const SAP_WALK_SLACK_MS = 3000;
        float const SAP_STAND_SEC = 30.0f;
        uint32 const SAP_CAST_TIMEOUT_MS = 4000;

        void Choose(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs);
        void Update(WorldModel const& world, PullPlan& pull, uint32 nowMs, std::vector<std::string>& notes);
    }
}

#endif
