/*
 * PartyBot Commander layer 2 (brain): an idle patrol walks over enemies standing away from the
 * pile before the fight ends (id "patrol"). From strat-20261102-225354: after the fight moved
 * off a patrol's path the tank walked back to win two casters back that stood at the old spot
 * with the rogue, and the patrol joined there.
 *
 * A patrol walking into the pile itself is the fight spot's (FightSpotAssigner, FightSpot.h:
 * the tank takes the pile elsewhere). Until 2026-11-14 this module also moved the fight (its
 * Move phase) and guarded only after such a move; now the guard follows the state:
 *
 * Pure (facts and plan only). While the party fights and the main tank (a bot) can act, for
 * each counted patrol that does not walk into the pile (SpotCost::PatrolJoinPoints 0 at the
 * pile: the fight spot's target while it moves, else the tank) and was not guarded until
 * GUARD_TIMEOUT_MS before this fight, the enemies it comes to (PatrolZone) before the fight
 * ends; the first patrol with any is guarded (TeamPlan::patrolGuard):
 *   avoid  = every refresh, those enemies. Nobody chases or walks over to them, they are not
 *            the focus; bots switch off them (TeamPlan.h, PatrolGuardPlan::avoid).
 *   lures  = a caster of `avoid` standing back (melee off, or a main ranged spell, more than
 *            LURE_MIN_DISTANCE from its victim) casting at a bot: that bot hides from it
 *            (LosLure; the point: PatrolPlanner::Step, LosBreak.h) and keeps fighting what
 *            it reaches there. Done when the casters came out of the path, within
 *            LURE_ARRIVED of the hider, turned to someone else or are gone; Failed after
 *            LURE_TIMEOUT_MS. One lure per hider and fight, at most MAX_LURE_CASTERS casters.
 *   kill   = casters of `avoid` standing back that no lure brings out (no point, failed,
 *            victim is a player or a pet, hider already used), and enemies of `avoid` that
 *            bring more (Tags alarm or summoner) and stand still (since 2026-11-26,
 *            strat-20261126-224046: an Eye of Naxxramas hovered in the path casting its
 *            summon, nobody might hit it): a ranged damage dealer kills each from where it
 *            stands (FocusAssigner intercept), worth `points` (the patrol joining a fight there).
 * Guard ends when no enemy stands in its path any more, the patrol walks into the pile now
 * (the fight spot moves it), joined the fight or left the scan, the tank changed, the retreat
 * or the pull's walk out / back took over, or after GUARD_TIMEOUT_MS (not guarded again).
 *
 * Reads plan.fightSpot as FightSpotAssigner left it this refresh or the last one: the pile's
 * place while the tank walks, a refresh late at worst.
 *
 * Known limits: the track covers at most WorldModelBuilder::PATROL_TRACK_MAX_SEC; facts have no
 * "moving" flag, so a caster already walking over may get a lure it does not need.
 */

#ifndef MANGOS_COMMANDER_PATROL_ASSIGNER_H
#define MANGOS_COMMANDER_PATROL_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct PartyMemberFacts;
    struct PatrolFacts;
    struct PatrolGuardPlan;

    class PatrolAssigner : public IAssigner
    {
    public:
        static constexpr uint32 GUARD_TIMEOUT_MS = 60 * 1000;
        // Lures (same numbers as the line of sight pull, PullPlanner / PullAssigner).
        static constexpr float LURE_MIN_DISTANCE = 10.0f;
        static constexpr float LURE_ARRIVED = 8.0f;
        static constexpr uint32 LURE_TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 MAX_LURE_CASTERS = 2;

        char const* GetId() const override { return "patrol"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // The plan's patrol among the facts, null when it joined the fight or left the scan.
        static PatrolFacts const* FindPatrol(WorldModel const& world, PatrolGuardPlan const& m);
    };
}

#endif
