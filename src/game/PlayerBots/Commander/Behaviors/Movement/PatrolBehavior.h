/*
 * PartyBot Commander layer 3 (cerebellum): the patrol guard (TeamPlan::patrolGuard,
 * PatrolAssigner: a patrol walks over enemies standing away from the pile). Until 2026-11-14 it
 * also walked the tank to a new fight spot; that is TankStanceBehavior's now (TeamPlan::fightSpot).
 *
 * Guard (until the patrol walked past):
 *   - Hider of an Active lure (PatrolLure): walks backwards to the lure's point and stays there
 *     (Backpedal renewed every tick, MoveTo as fallback); not a healer and its target is one of
 *     the casters or off limits: Attack the nearest other enemy within HIDE_MELEE_REACH of the
 *     point (melee) or TARGET_RANGE (ranged), so it keeps fighting while it waits.
 *   - Anyone else but healers: its target is in PatrolGuardPlan::avoid (and not its intercept)
 *     and out of melee reach: the brain gives it another target (TargetAssigner).
 */

#ifndef MANGOS_COMMANDER_PATROL_BEHAVIOR_H
#define MANGOS_COMMANDER_PATROL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PatrolBehavior : public IBehavior
    {
    public:
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
