/*
 * PartyBot Commander layer 2 (brain): an idle patrol walks into the fight before it ends.
 * From strat-20261101-225111: Patchwork Horror (17000 hp elite, ~70 s loop) walked through the
 * fight spot 28 s into a 7-elite pull and the party wiped.
 *
 * Pure (facts and plan only). While the party fights, no move is running and the tank holds
 * most enemies, every CHECK_MS: the tank's place against the field cells around it
 * (Coordination/Situation/FightSpot/FightSpot.h, battlefield groups doc 8, user 2026-11-13:
 * on the position field; it loses points to a patrol walking in or to a control that breaks
 * on damage next to the pile). A better cell: TeamPlan::patrolMove with its spots
 * (PatrolBehavior walks the tank there), the patrol costing the most as `patrol` (none for a
 * control only). Else the party stays and the leader is told why (once per reason).
 * While the pull still places the tank (Settle with the arrival wait or an Active line of sight
 * pull, whose point already avoids patrol paths) only patrols arriving within
 * PULL_PLACING_DEFER_SEC count.
 * Until 2026-11-13: per patrol, benefit (its damage) > cost (the walk) to the first of rings at
 * 15 / 25 / 35 yd x 12 where the whole party shifted with the tank was clear of its walk, and
 * every bot walked that shift. Now the others pick their own cells (stance modules).
 *
 * Move phase ends when the tank arrived or after MOVE_TIMEOUT_MS; without a patrol the plan
 * ends there; else Guard (strat-20261102-225354: after the move the tank walked back to win two casters back
 * that stood at the old spot with the rogue, and the patrol joined there):
 *   avoid  = every refresh while active, enemies the patrol comes to (PatrolZone) before the
 *            fight ends. Nobody chases or walks over to them, they are not the focus; in
 *            Guard bots switch off them (TeamPlan.h, PatrolMovePlan::avoid).
 *   lures  = Guard only. A caster of `avoid` standing back (melee off, or a main ranged
 *            spell, more than LURE_MIN_DISTANCE from its victim) casting at a bot: that bot
 *            hides from it (PatrolLure; the point: PatrolPlanner::Step, LosBreak.h) and keeps
 *            fighting what it reaches there. Done when the casters came out of the path,
 *            within LURE_ARRIVED of the hider, turned to someone else or are gone; Failed after
 *            LURE_TIMEOUT_MS. One lure per hider and fight, at most MAX_LURE_CASTERS casters.
 *   kill   = casters of `avoid` standing back that no lure brings out (no point, failed,
 *            victim is a player or a pet, hider already used): a ranged damage dealer kills
 *            each (FocusAssigner intercept).
 * Guard ends when the old fight spot is clear of the patrol's walk for the rest of the fight
 * and no enemy stands in its path, or after GUARD_TIMEOUT_MS.
 * The plan ends at once when no spot passed the live checks, the patrol joined the fight or
 * left the scan, the tank changed, or the retreat or a pull took over.
 *
 * Known limits: the track covers at most WorldModelBuilder::PATROL_TRACK_MAX_SEC; the spot
 * geometry (ground, idle creatures, path) is only checked by the behavior; facts have no
 * "moving" flag, so a caster already walking over may get a lure it does not need.
 */

#ifndef MANGOS_COMMANDER_PATROL_ASSIGNER_H
#define MANGOS_COMMANDER_PATROL_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct PartyMemberFacts;
    struct PatrolFacts;
    struct PatrolMovePlan;
    struct Position3;

    class PatrolAssigner : public IAssigner
    {
    public:
        static constexpr uint32 CHECK_MS = 1000;
        static constexpr float ARRIVED_DISTANCE = 4.0f;
        static constexpr uint32 MOVE_TIMEOUT_MS = 15 * 1000;
        static constexpr uint32 GUARD_TIMEOUT_MS = 60 * 1000;
        // Lures (same numbers as the line of sight pull, PullPlanner / PullAssigner).
        static constexpr float LURE_MIN_DISTANCE = 10.0f;
        static constexpr float LURE_ARRIVED = 8.0f;
        static constexpr uint32 LURE_TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 MAX_LURE_CASTERS = 2;
        // While the pull still places the tank (arrival wait, line of sight pull), a patrol
        // arriving later than this is judged afterwards: the line of sight break lasts at most
        // PullAssigner::LOS_BREAK_TIMEOUT_MS (8 s), plus a move of about 15 yd.
        static constexpr float PULL_PLACING_DEFER_SEC = 20.0f;

        char const* GetId() const override { return "patrol"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // The plan's patrol among the facts, null when it joined the fight or left the scan.
        static PatrolFacts const* FindPatrol(WorldModel const& world, PatrolMovePlan const& m);

    private:
        void Continue(WorldModel const& world, TeamPlan& plan, PartyMemberFacts const& tank) const;
    };
}

#endif
