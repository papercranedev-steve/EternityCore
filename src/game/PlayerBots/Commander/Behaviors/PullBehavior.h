/*
 * PartyBot Commander layer 3: executes TeamPlan::pull (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 *  Tank     Approach: attack the chosen target (original chase / charge brings it there).
 *           Return:   run back to the fight spot; the enemies follow.
 *  Damage   Approach / Return / Settle: stay where they are, do not attack.
 *  Healer   Approach / Return: stay put while out of combat; heal normally once in combat.
 */

#ifndef MANGOS_COMMANDER_PULL_BEHAVIOR_H
#define MANGOS_COMMANDER_PULL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PullBehavior : public IBehavior
    {
    public:
        static constexpr float RUN_SPEED_YD_PER_SEC = 7.0f;

        char const* GetId() const override { return "pull"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
