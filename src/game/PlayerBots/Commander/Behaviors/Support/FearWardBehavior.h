/*
 * PartyBot Commander layer 3 (cerebellum): a dwarf priest bot casts Fear Ward in a fight on
 * the member the brain names (TeamPlan::fearWards, FearWardAssigner.h; boss doc G8) when a
 * sure fear is coming on it. Assigned tier like the heal orders: value = the duty's points
 * (the control it saves), cost = the priest's global cooldown and the mana. A duty with 0
 * points (only kept up on the tank) is the rotation's between fights, not cast here.
 */

#ifndef MANGOS_COMMANDER_FEAR_WARD_BEHAVIOR_H
#define MANGOS_COMMANDER_FEAR_WARD_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class FearWardBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "fear ward"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
