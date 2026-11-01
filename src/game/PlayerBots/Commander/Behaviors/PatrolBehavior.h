/*
 * PartyBot Commander layer 3: walks the tank to the spot TeamPlan::patrolMove chose
 * (PatrolAssigner: an idle patrol would walk into the fight). The enemies chase the tank, the
 * damage dealers chase their targets and the healers anchor on the tank, so only the tank moves.
 *
 * Tank of the plan only. Once per move: the plan's spots in order, the first whose ground is
 * found within MAX_STEP_Z of the tank, that no idle creature covers, and whose path is safe
 * (SafeRoute::PathIsSafe, MAX_PATH_DETOUR) is written to patrolMove.chosen (-2: none). Then
 * MoveTo there (tier Assigned, valued with the plan's benefit and cost), and Hold while the
 * walk is under way.
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

        char const* GetId() const override { return "patrol"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
