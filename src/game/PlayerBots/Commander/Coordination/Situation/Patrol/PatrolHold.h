/*
 * PartyBot Commander layer 2 (brain), situation step: the patrol the pull holds, before it
 * comes (battlefield groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md
 * 7; user 2026-11-13: the controller goes early and holds it outside, not next to the tank).
 * A reservation (directive kind 4): planned now, the cerebellum casts when the condition holds
 * (CrowdControlBehavior, ProposePatrolHold). Writes TeamPlan::patrolHold.
 *
 * Active while the doctrine holds a patrol from the pull's plan (DoctrineHold::planned, made by
 * TacticalDoctrine from PullPlan::holdPatrol; registered after it, so this refresh's) and that
 * patrol is still idle (WorldModel::patrols). Once it is in the fight the doctrine's hold is a
 * duty (CrowdControlAssigner): a resist is cast again from where the controller stands.
 *
 *   fightAt   the main tank in the fight, else the pull's planned fight spot;
 *   clearYd   ControlValue::ClearOfPileYd;
 *   holdAt    PatrolZone::FirstWithin(patrol, fightAt, clearYd): the track point before it
 *             (its time holdSec); already within: where it is, holdSec 0;
 *   spots     position field cells (FieldSpots::Collect: standable, no idle creature) within
 *             the spell's range - RANGE_SLACK_YD of holdAt that the patrol's aggro zone does not
 *             reach before holdSec + ARRIVE_SLACK_SEC (PatrolZone::ArriveSec), shortest walk
 *             first, MAX_SPOTS of them (line of sight is the cerebellum's, live);
 *   goAtMs    the refresh + holdSec - the walk to the first spot (RUN_YARDS_PER_SEC, PATH_FACTOR)
 *             - LEAD_SEC: in place a little before it comes.
 * No position field (or no spot): no spots; the controller casts from where it is when the
 * patrol walks into range.
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_PATROL_HOLD_H
#define MANGOS_COMMANDER_PATROL_HOLD_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class PatrolHoldAssigner : public IAssigner
    {
    public:
        static constexpr float RANGE_SLACK_YD = 3.0f;
        static constexpr float ARRIVE_SLACK_SEC = 2.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr float LEAD_SEC = 4.0f;
        static constexpr size_t MAX_SPOTS = 4;

        char const* GetId() const override { return "patrolhold"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
