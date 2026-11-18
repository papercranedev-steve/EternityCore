/*
 * PartyBot Commander layer 3: executes interrupt duties from InterruptAssigner (step 4),
 * scored as benefit - cost (value-scoring doc 5.1).
 * Design: .kiro/docs/2026-11-01-interrupt-assignment.md
 *
 *  Primary - interrupt as soon as the cast is seen. Melee primaries out of range switch
 *            to the caster so the original chase logic brings them in.
 *  Backup  - interrupt when the primary cannot (controlled, no ready ability that the enemy
 *            is not immune to, busy casting, gone) or the cast is past BACKUP_AFTER_PROGRESS.
 *  Abilities the enemy is immune to (CastValues::CanStop) are never proposed; each ability's
 *            benefit includes its school lockout (CastValues::LockoutBenefit).
 *  Only    - the class rotations have no reflex interrupts (deleted 2026-11-14): only duties
 *            from the plan are cast. A cast nobody was assigned to is one not worth an interrupt.
 *  Urgent tier only when the cast would kill someone; otherwise team-task tier.
 *  Timing  - a refresh that sees the duty's enemy casting wakes the primary and the backup
 *            (WantsTickNow), so they do not wait for their next bot interval. A bot busy with
 *            its own cast stops it when waiting would miss the enemy cast (CastBreakBenefit).
 */

#ifndef MANGOS_COMMANDER_INTERRUPT_BEHAVIOR_H
#define MANGOS_COMMANDER_INTERRUPT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class InterruptBehavior : public IBehavior
    {
    public:
        static constexpr float BACKUP_AFTER_PROGRESS = 0.5f;
        static constexpr float MELEE_ABILITY_RANGE = 5.0f;
        // Own cast: from its end to the interrupt going out (the next frame's bot update and
        // the cast). An enemy cast ending sooner than own cast left + this is missed by waiting.
        static constexpr uint32 CAST_BREAK_SLACK_MS = 300;

        char const* GetId() const override { return "interrupt"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        // The bot is primary or backup of a duty whose enemy casts now.
        bool WantsTickNow(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        // Own cast: the duty's enemy cast ends before it does (CAST_BREAK_SLACK_MS), nobody free
        // takes it instead (primary: the backup is free and ready; backup: the primary can act
        // once its own cast stops), and an ability is ready now (cooldown and GCD). Worth: the
        // best ability's benefit - cost, as in Propose.
        float CastBreakBenefit(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               uint32 castLeftMs, char const*& reason) const override;
    };
}

#endif
