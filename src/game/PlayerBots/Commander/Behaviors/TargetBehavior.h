/*
 * PartyBot Commander layer 3 (cerebellum): attack the enemy the brain gave this bot
 * (TeamPlan::targets, Coordination/TargetAssigner.h). Replaces FocusBehavior and the original
 * PartyBot target selection (2026-11-07 target assignment).
 * Design: .kiro/docs/2026-11-07/2026-11-07-target-assignment.md 2.3
 *
 * When the plan's enemy is not the bot's victim: `Attack` it.
 *   forced (player order, brain task)   Urgent, not valued
 *   urgent (intercept, deadly loose)    Urgent,   benefit - cost from the plan
 *   otherwise                           Assigned, benefit - cost from the plan
 * Live checks only: the enemy is still a valid target (PartyBotAI::IsValidHostileTarget), and a
 * ranged damage dealer sees it within RANGED_SIGHT_RANGE (melee and tanks walk there).
 * Tanks, melee and ranged damage dealers; healers never get a target.
 */

#ifndef MANGOS_COMMANDER_TARGET_BEHAVIOR_H
#define MANGOS_COMMANDER_TARGET_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class TargetBehavior : public IBehavior
    {
    public:
        static constexpr float RANGED_SIGHT_RANGE = 40.0f;
        // Used by boss modules proposing their own switches (DeadminesVanCleef).
        static constexpr float FOCUS_RANGE = 30.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;

        char const* GetId() const override { return "target"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
