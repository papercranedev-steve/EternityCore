/*
 * PartyBot Commander layer 2 (brain): who keeps a stun, root or slow for an enemy about to run
 * away (flee interception, todo 13, .kiro/docs/2026-11-08-flee-interception.md). Duties module.
 *
 * The fallback after the focus (user, 2026-11-08): the party turns to such an enemy in time
 * (FocusAssigner's flee bonus, TargetAssigner's suggested switch); a control on it is only
 * offered when that still lets it run far (FleeFacts::focusedPoints > 0), valued by
 * Perception/FleeStopValue.h (wasted on a small enemy the party kills anyway: no net).
 *
 * Offer, while the party fights: one need per enemy with a prediction (FleeFacts::predicted,
 * not spent, not prevented). now = it runs, or its health is within TRIGGER_MARGIN_PCT of the
 * flee line; before that it is a reservation (now = false: DutyAllocator answers it after
 * every live need, with the bots left over). Candidates: every bot that can act with a ready
 * FleeStopAbility of positive net, within its range + RESERVE_SLACK_YD (a melee one: the bot
 * is on it already); a finisher (Kidney Shot) only with combo points on it, or attacking it
 * (it builds them). rank = net (+ STICKY_POINTS for last refresh's bot). A slow only once it
 * runs is offered as such too (FleeStopDuty::hold = false).
 * Apply: plan.fleeStops.
 *
 * Who executes: FleeStopBehavior (live trigger, cast). TeamPlan::HoldsSpell keeps the spell
 * (and a finisher's other finishers) from the rotation and from other modules until then.
 */

#ifndef MANGOS_COMMANDER_FLEE_STOP_ASSIGNER_H
#define MANGOS_COMMANDER_FLEE_STOP_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class FleeStopAssigner : public IAssigner
    {
    public:
        // A hold is cast this close above the flee line (the hit that crosses it may come
        // between two ticks). Untested guess.
        static constexpr float TRIGGER_MARGIN_PCT = 2.0f;
        static constexpr float RESERVE_SLACK_YD = 5.0f;
        static constexpr float STICKY_POINTS = 0.5f;

        // Same id as FleeStopBehavior.
        char const* GetId() const override { return "fleestop"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
