/*
 * PartyBot Commander layer 2 (brain), situation step: where the main tank keeps the pile, every
 * refresh (tank fight spot doc .kiro/docs/2026-11-14-tank-fight-spot.md, step 1; id
 * "fightspot"). Until 2026-11-14 PatrolAssigner did this every CHECK_MS, as its move phase.
 *
 * While the party fights, the main tank (a bot) can act and holds most enemies (at least half
 * of the acting, uncontrolled ones fighting the party hit it): FightSpot::Choose, the tank's
 * place against the field cells around it (FightSpot.h: patrols walking in, controls that
 * break on damage next to the pile, the walk). Nothing to gain there, no search (cheap every
 * refresh). A better cell: TeamPlan::fightSpot moves, with its spots (TankStanceBehavior walks
 * the tank there, backwards with the pile on it). No better cell: the leader is told why, once
 * per reason.
 * While the pull still places the tank (PullPlan::PlacesTank in Settle: waiting for the pack,
 * hiding from casters) only patrols arriving within PULL_PLACING_DEFER_SEC count; the move then
 * competes with the pull's walk at the arbiter (its benefit - cost against the pull's score).
 *
 * The move ends when the tank arrived (ARRIVED_DISTANCE of the chosen spot), after
 * MOVE_TIMEOUT_MS, when no spot passed the live checks, when the tank lost most enemies on the
 * way (it takes them back first), the tank changed, or the retreat or the pull's walk out /
 * back took over. From the new place the next refresh judges again.
 *
 * Before the fight (PlanAhead, doc step 2): a pull fought "here" (a walk-in where the pack
 * stands, a shot or face pull where the tank opens; not brought back, not a corner, doc 9
 * answer 3) is judged once for the pull's spot (PullPlan::PullFightSpot) before the opener, with
 * the pull's fight estimate (PullPlan::expectedFightSec, else RouteGuard::PULL_FALLBACK_FIGHT_SEC)
 * and the walk from that spot (FightSpot::Request::walkFromAt). Judged again when the pull is
 * planned again or its spot moved more than REJUDGE_YD. A better cell: FightSpotPlan::planned;
 * the members take their places for it (TeamPlan::PlannedFightSpot) and the move starts with
 * its spots as soon as the fight is on, the tank holds most enemies and the pull no longer
 * places the tank (a walk-in: once the opener landed; a shot or face pull: once the pack came
 * to where the tank opened). The tank opens as planned (Charge included) and takes the pile there.
 *
 * Reads plan.area (FightSpot's area end cost) and plan.pull as the earlier assigners left them
 * this refresh or the last one (AreaPhase runs after this): one refresh late at worst.
 */

#ifndef MANGOS_COMMANDER_FIGHT_SPOT_ASSIGNER_H
#define MANGOS_COMMANDER_FIGHT_SPOT_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"
#include "ObjectGuid.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

    class FightSpotAssigner : public IAssigner
    {
    public:
        static constexpr float ARRIVED_DISTANCE = 4.0f;
        static constexpr uint32 MOVE_TIMEOUT_MS = 15 * 1000;
        // While the pull still places the tank (arrival wait, line of sight pull), a patrol
        // arriving later than this is judged afterwards: the line of sight break lasts at most
        // PullAssigner::LOS_BREAK_TIMEOUT_MS (8 s), plus a move of about 15 yd.
        static constexpr float PULL_PLACING_DEFER_SEC = 20.0f;
        // The pull's spot moved this far (a new shot spot): the plan is judged again.
        static constexpr float REJUDGE_YD = 4.0f;

        char const* GetId() const override { return "fightspot"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // At least half of the acting, uncontrolled enemies fighting the party hit `tank`.
        static bool HoldsMost(WorldModel const& world, ObjectGuid tank);

    private:
        void PlanAhead(WorldModel const& world, TeamPlan& plan) const;
        void Continue(WorldModel const& world, TeamPlan& plan, PartyMemberFacts const& tank) const;
    };
}

#endif
