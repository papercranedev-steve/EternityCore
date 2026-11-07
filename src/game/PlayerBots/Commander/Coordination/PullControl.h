/*
 * PartyBot Commander layer 2 (brain): opening control of a pull (crowd control doc
 * .kiro/docs/2026-11-07/2026-11-07-crowd-control.md 4.3, todo 10c). Its own file so the pull
 * planner only calls it (the pull files are split by step, PullPlanner.h).
 *
 * Choose (map thread, when the tank is about to go: PullPlanner::RecheckBeforeApproach):
 *   the fight against the expected creatures (FightLoad::Build) with and without one of them
 *   controlled from the start, for every pulled creature but the pull target (it must come to
 *   the tank) and every bot but the tank with a ready single target control that affects it
 *   (ControlValue::Affects; no fears, no Sap yet): ControlValue::OfPull. nearTarget = the
 *   other expected creatures when the fight is where they stand (walk-in, corner), 0 when
 *   it is elsewhere (brought back, opened from range or by a face pull: the controlled one
 *   stays at its place).
 *   Best net first, net > 0: the controller needs a spot (ShotSpot::Find: the target sees it,
 *   within the spell's range - RANGE_MARGIN_YD, out of every idle creature's aggro range,
 *   off every patrol's way for STAND_SEC, a safe walk); up to MAX_SPOT_TRIES options.
 *   Whether to control at all is the score: an easy pull gains nothing (no danger, little
 *   damage saved) and gets none.
 *   Always sets PullControlPlan::decided (the tank waits for it).
 * Update (pure, PullAssigner on every refresh):
 *   WaitReady: forget the decision (the pull was held back: decide again next time).
 *   Approach before the fight: the controller within ARRIVED_YD of the spot -> inPlace, the
 *   Approach timers start again (the tank waited); not there after PLACE_TIMEOUT_MS or it
 *   cannot act -> dropped with a note, the tank pulls without it.
 * Execution: CrowdControlBehavior walks the controller to the spot and casts once the fight
 * started (the need comes from CrowdControlAssigner, the plan's points); the tank holds its
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
        // The controller stands at its spot before the fight: the walk there (at most
        // PLACE_TIMEOUT_MS) plus the tank's way to its opener. No patrol may pass it meanwhile;
        // once the fight runs, a patrol joining is the pull plan's (PullPlanner patrol check).
        float const STAND_SEC = PLACE_TIMEOUT_MS / 1000.0f + 5.0f;


        void Choose(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs);
        void Update(WorldModel const& world, PullPlan& pull, uint32 nowMs, std::vector<std::string>& notes);
    }
}

#endif
