/*
 * PartyBot Commander layer 2 (brain), situation: a caster standing back after the pull (id
 * "casterlure"). strat-20261115-183927: a Shrieking Banshee freed from its shackle mid-fight
 * stood 24-30 yd off casting at the priest for 13 s; the tank finally walked over to it and
 * pulled the pack it stood next to. The pull's line of sight break (PullPlan::losBreak) runs
 * in Settle only, the patrol guard's lures (PatrolGuardPlan::lures) only in a patrol's path.
 *
 * Pure (facts and plan). While the party fights, the pull is over (PullPhase::None) and no
 * retreat runs, every enemy that stands back (StandsBack: acting, not crowd controlled, melee
 * off or a main ranged spell and not moving, more than MIN_DISTANCE from its victim), not in
 * a patrol's path (the patrol guard's), casting at a bot (not a pet) that can act: that bot
 * hides from it (TeamPlan::casterLure, LosLure; the point: CasterLurePlanner::Step on the map
 * thread). It keeps fighting what it reaches there (LureBehavior); its stance module stands
 * down (TeamPlan::HidingFor). A healer hides behind the tank as seen from the caster
 * (LosHealerSpot) so the caster walks past the tank.
 *   one lure per caster and fight (a caster that did not come is fought from range: the
 *   rotations, the focus), at most MAX_CASTERS per lure, one lure at a time per hider; the
 *   main tank not while the fight spot moves it (FightSpotPlan, asked again next refresh).
 *   Done when every caster came within ARRIVED of the hider, turned to someone else, is
 *   crowd controlled or gone, the hider cannot act or the fight spot moves it (a patrol
 *   coming soon: FightSpotAssigner defers other moves while the tank hides); Failed after
 *   TIMEOUT_MS (a note).
 * The pull or a retreat taking over ends the lures going on. Same numbers as the pull's break
 * and the patrol guard's lures.
 */

#ifndef MANGOS_COMMANDER_CASTER_LURE_ASSIGNER_H
#define MANGOS_COMMANDER_CASTER_LURE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct EnemyFacts;

    class CasterLureAssigner : public IAssigner
    {
    public:
        static constexpr float MIN_DISTANCE = 10.0f;
        static constexpr float ARRIVED = 8.0f;
        static constexpr uint32 TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 MAX_CASTERS = 2;

        char const* GetId() const override { return "casterlure"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // Pure: `e` stands back and casts at its victim instead of walking to it (header).
        static bool StandsBack(WorldModel const& world, EnemyFacts const& e);
    };
}

#endif
