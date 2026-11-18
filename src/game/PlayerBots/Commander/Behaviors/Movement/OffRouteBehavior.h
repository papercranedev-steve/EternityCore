/*
 * PartyBot Commander layer 3 (cerebellum): walks a bot off an idle patrol's route when the
 * brain says so (TeamPlan::routeGuard, RouteGuard.h; doc .kiro/docs/2026-11-10-position-field.md 16).
 *
 * The brain's task is a must (user 2026-11-10: "if the brain says so, move first, whatever the
 * bot is doing"): Urgent tier, above every stance module, pull hold and rotation order.
 *  - Walking already under a movement hold to a spot off the route: Hold (keep walking; a
 *    cast-time spell would stop the walk).
 *  - Else the nearest cell around the bot (FieldSpots::Collect, up to RouteGuard::LEAVE_SEARCH_YD,
 *    no patrol before RouteGuard::StaySec), shortest walk first, free cells before taken ones
 *    (FieldSpots::PreferFree); live checks on at most MAX_GEOMETRY_CHECKS: this bot's own idle
 *    scan at the end and SafeRoute::PathIsSafe. MoveTo, score = -walk seconds.
 * Where to stand for its job (heal range, cast range, the mob's back) is the stance modules'
 * question afterwards: they pick from the same cells, so they do not bring it back.
 *
 * A cast in progress (CastBreakBenefit; CastBreak runs only in combat): stopped only when the
 * patrol would arrive before the cast ends plus the walk out (task leaveSec) plus
 * CAST_MARGIN_SEC (user 2026-11-10: a heal ending in 1 s with the patrol 8 s off is finished
 * first); then it is worth the task's points (the patrol joining).
 * WantsTickNow while it has a task: the next tick in the next frame, not a bot interval later.
 *
 * Out of combat the arbiter runs only while a pull or a fight goes on
 * (PartyCommander::WantsOutOfCombatControl); travelling is the follow spot's (FollowSpot).
 */

#ifndef MANGOS_COMMANDER_OFF_ROUTE_BEHAVIOR_H
#define MANGOS_COMMANDER_OFF_ROUTE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class OffRouteBehavior : public IBehavior
    {
    public:
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 5;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;
        static constexpr float CAST_MARGIN_SEC = 1.5f;

        char const* GetId() const override { return "offroute"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        bool WantsTickNow(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        float CastBreakBenefit(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               uint32 castLeftMs, char const*& reason) const override;
    };
}

#endif
