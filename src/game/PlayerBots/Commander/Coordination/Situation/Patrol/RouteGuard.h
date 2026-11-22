/*
 * PartyBot Commander layer 2 (brain, situation): keep every bot off an idle patrol's route
 * while we stay here (user 2026-11-10, doc .kiro/docs/2026-11-10-position-field.md 16).
 *
 * Before, each stance module filtered patrol cells itself, with the fight's remaining time:
 * 0 before the fight (no enemy engaged yet), so a mage took its place for the pull on a
 * patrol's route and pulled the patrol before the tank pulled the pack. Melee and bots that
 * already stood still were never checked.
 *
 * StaySec, how long we stay where we are:
 *  - the party fights: Value::FightRemainingSec;
 *  - a pull runs: the pull's estimated fight (PullPlan::expectedFightSec, else
 *    PULL_FALLBACK_FIGHT_SEC) + PullPlanner::PULL_LEAD_SEC (the tank walking out and back);
 *  - otherwise (travelling): TRAVEL_STAY_SEC, the time to step aside; checked again on every
 *    refresh, so a patrol further off is seen when it comes closer.
 * The stance modules pass the same to FieldSpots (patrolBeforeSec), so they never pick a spot
 * on the route either.
 *
 * Assign, every refresh with a position field: for each bot member a patrol comes to
 * (PatrolZone::ArriveSec; where it stands or walks to: PartyMemberFacts::standAt; while it
 * walks, also where it is now) before StaySec, an OffRouteTask:
 *   patrol  = the first patrol to come to the spot (Perception/Spatial/PatrolZone.h, the rule
 *             every patrol check uses);
 *   points  = Value::Points(its melee x (StaySec - arrival)) + Value::FightExtension(its kill
 *             time): what it costs when it joins (as PatrolAssigner's benefit);
 *   leaveSec= grid walk to the nearest cell off the route within LEAVE_SEARCH_YD, at
 *             RUN_YARDS_PER_SEC (straight line x PATH_FACTOR without a grid walk).
 * Not for: dead members, players and pets (not ours to move); everyone during a retreat; the
 * pull's tank while it goes out or brings the pack back (Approach / Return, the pull owns its
 * walk), the opening controller walking to its spot (PullControl, its spot checks patrols),
 * line of sight hiders; the tank while it moves the fight (TeamPlan::fightSpot). Not against
 * the patrols of the pack the team hunts (TeamPlan::hunt): that fight is the one we walk to. In a fight a
 * member whose main tank stands on the route too: the whole fight is on it, the fight spot's
 * question (FightSpotAssigner), not one member's.
 *
 * Reads the pull plan and fightSpot as this refresh's earlier assigners left them (or the
 * last refresh's): only who is exempt depends on it, a refresh later at worst.
 */

#ifndef MANGOS_COMMANDER_ROUTE_GUARD_H
#define MANGOS_COMMANDER_ROUTE_GUARD_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    namespace RouteGuard
    {
        float const PULL_FALLBACK_FIGHT_SEC = 30.0f;
        float const TRAVEL_STAY_SEC = 8.0f;
        float const LEAVE_SEARCH_YD = 20.0f;
        float const RUN_YARDS_PER_SEC = 7.0f;
        float const PATH_FACTOR = 1.3f;

        // How long we stay where we are (header). Also the stance modules' patrol horizon.
        float StaySec(WorldModel const& world, TeamPlan const& plan);
    }

    class RouteGuardAssigner : public IAssigner
    {
    public:
        char const* GetId() const override { return "routeguard"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
