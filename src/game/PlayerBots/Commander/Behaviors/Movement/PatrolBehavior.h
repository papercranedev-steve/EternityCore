/*
 * PartyBot Commander layer 3 (cerebellum): walks the tank to the spot TeamPlan::patrolMove
 * chose (PatrolAssigner: an idle patrol would walk into the fight), and every other bot the
 * same shift (melee too since strat-20261102-225354). The enemies chase the tank.
 *
 * Guard (after the move, until the patrol walked past):
 *   - Hider of an Active lure (PatrolLure): walks backwards to the lure's point and stays there
 *     (Backpedal renewed every tick, MoveTo as fallback); not a healer and its target is one of
 *     the casters or off limits: Attack the nearest other enemy within HIDE_MELEE_REACH of the
 *     point (melee) or TARGET_RANGE (ranged), so it keeps fighting while it waits.
 *   - Anyone else but healers: its target is in PatrolMovePlan::avoid (and not its intercept)
 *     and out of melee reach: Attack the nearest enemy within TARGET_RANGE not in avoid.
 *
 * Fix: before, only the tank moved. A healer anchors on the tank but only moves once the tank
 * is out of heal range (HealerStanceBehavior), ranged keep their 25 yd chase distance: a 15-25
 * yd move left them standing in the patrol's path, and a healer pulled the patrol from behind.
 *
 * Tank of the plan: once per move, the plan's spots in order, the first whose ground is
 * found within MAX_STEP_Z of the tank, that no idle creature covers, and whose path is safe
 * (SafeRoute::PathIsSafe, MAX_PATH_DETOUR) is written to patrolMove.chosen (-2: none). Then
 * walk there (tier Assigned, valued with the plan's benefit and cost): backwards facing the
 * tank's victim (Backpedal, order renewed every tick, the rotation keeps fighting), or
 * MoveTo when no backwards path is found or the tank has no victim, with Hold while that
 * walk is under way.
 *
 * Followers of the plan (TeamPlan.h, PatrolFollower): once the tank's spot is chosen, target =
 * own start + (chosen spot - tank start), the place PatrolAssigner checked against the
 * patrol's walk. The same ground / idle creature / path checks once (failed: stay, the stance
 * module keeps the healer in range); then MoveTo (Hold while walking), after a cast in progress.
 */

#ifndef MANGOS_COMMANDER_PATROL_BEHAVIOR_H
#define MANGOS_COMMANDER_PATROL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PatrolBehavior : public IBehavior
    {
    public:
        static constexpr float MAX_STEP_Z = 6.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 1000;
        static constexpr uint32 BACKPEDAL_ORDER_MS = 1500;
        static constexpr float HIDE_MELEE_REACH = 5.0f;     // from the point to the enemy's edge
        static constexpr float TARGET_RANGE = 30.0f;

        char const* GetId() const override { return "patrol"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
