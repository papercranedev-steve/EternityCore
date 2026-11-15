/*
 * PartyBot Commander layer 3 (cerebellum): the hider of a lure (TeamPlan::HidingFor: the
 * patrol guard's, PatrolAssigner, or a caster standing back after the pull,
 * CasterLureAssigner). Was PatrolBehavior until 2026-11-15 (only the patrol guard's lures).
 *
 * Walks backwards to the lure's point and stays there (Backpedal renewed every tick, MoveTo as
 * fallback); not a healer and its target is one of the casters or off limits: Attack the
 * nearest other enemy within HIDE_MELEE_REACH of the point (melee) or TARGET_RANGE (ranged),
 * so it keeps fighting while it waits. The hider's stance module stands down meanwhile
 * (TankStanceBehavior, HealerStanceBehavior, RangedStanceBehavior).
 */

#ifndef MANGOS_COMMANDER_LURE_BEHAVIOR_H
#define MANGOS_COMMANDER_LURE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class LureBehavior : public IBehavior
    {
    public:
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 1000;
        static constexpr uint32 BACKPEDAL_ORDER_MS = 1500;
        static constexpr float HIDE_MELEE_REACH = 5.0f;     // from the point to the enemy's edge
        static constexpr float TARGET_RANGE = 30.0f;

        char const* GetId() const override { return "lure"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
