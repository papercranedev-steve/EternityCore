/*
 * PartyBot Commander layer 2 (brain), map-thread part of the caster lure (CasterLureAssigner.h).
 * PartyCommander::Update calls Step right after the assigners.
 *
 * Step: every lure in Search gets a point (LosBreak.h), hidden from its casters, not in any
 * patrol's path before the fight ends (PatrolZone), not towards an idle creature
 * (LosBreak::TowardsIdleCreature):
 *   a healer hider: first behind the main tank as seen from the casters (LosHealerSpot: in
 *     sight of the tank, the casters' walk passes it), else
 *   anyone: around the hider, seen by the main tank and the healer bots (LosBreak::Watchers).
 * Found: Active from now (LureBehavior walks the hider there). Not found: NoPoint, the casters
 * are fought from range. A note for the leader either way.
 */

#ifndef MANGOS_COMMANDER_CASTER_LURE_PLANNER_H
#define MANGOS_COMMANDER_CASTER_LURE_PLANNER_H

#include "Common.h"

class Map;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    namespace CasterLurePlanner
    {
        void Step(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs);
    }
}

#endif
