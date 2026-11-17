/*
 * PartyBot Commander layer 3: walks the push mode's guide (TeamPlan::push, PushPlanner) along
 * its leg. Only the guide; the others follow it (Guide.h, the normal follow).
 *
 * While the leg runs: MoveTo the leg's end (Assigned tier, source "push"), renewed only when
 * the guide stopped or the hold ran out (a new MovePoint every tick restarts the spline);
 * otherwise Hold, so nothing out of combat (a buff, a drink) stops the walk. The planner judges
 * arrival and getting stuck. No leg: its own walk is stopped (StopMove, "push" only).
 */

#ifndef MANGOS_COMMANDER_PUSH_BEHAVIOR_H
#define MANGOS_COMMANDER_PUSH_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PushBehavior : public IBehavior
    {
    public:
        // Paths are longer than the straight line; the hold is renewed when it runs out anyway.
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 1000;

        char const* GetId() const override { return "push"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
