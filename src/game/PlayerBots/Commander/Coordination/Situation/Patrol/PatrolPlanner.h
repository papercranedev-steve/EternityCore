/*
 * PartyBot Commander layer 2 (brain), map-thread part of the patrol guard (PatrolAssigner.h).
 * PatrolAssigner is pure and cannot check line of sight; PartyCommander::Update calls Step
 * right after the assigners.
 *
 * Step: every lure in Search (casters standing back in the patrol's path, casting at the
 * hider) gets a point from LosBreak::FindPoint around the hider: hidden from the casters,
 * seen by the main tank and the healer bots (not the hider itself; LosBreak::Watchers), not
 * in the patrol's path, not towards an idle creature (LosBreak::TowardsIdleCreature). Found:
 * Active from now (the hider walks there, LureBehavior). Not found: NoPoint (PatrolAssigner kills the casters
 * from range). A note for the leader either way.
 */

#ifndef MANGOS_COMMANDER_PATROL_PLANNER_H
#define MANGOS_COMMANDER_PATROL_PLANNER_H

#include "Common.h"

class Map;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    namespace PatrolPlanner
    {
        void Step(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs);
    }
}

#endif
