/*
 * PartyBot Commander layer 3: executes TeamPlan::pull (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 *  Tank     Approach: walk-in pull: attack the chosen target (original chase / charge brings it there).
 *                     Ranged pull (PullPlan::rangedPullSpell): walk to a point inside
 *                     rangedPullRange on the line to the target, step closer while there is no
 *                     line of sight, then cast the spell (Shoot / Throw / Faerie Fire (Feral) /
 *                     Auto Shot). Closer than the spell's minimum range: walk-in pull.
 *           Return:   run back to the fight spot; the enemies follow.
 *  Others   Approach / Return / Settle: stay where they are while out of combat.
 *           Once in combat (healers and damage dealers alike) the original AI acts normally.
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
        // Ranged pull in range without line of sight: walk this far towards the target, look again.
        static constexpr float LOS_STEP_YD = 4.0f;

        char const* GetId() const override { return "pull"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
