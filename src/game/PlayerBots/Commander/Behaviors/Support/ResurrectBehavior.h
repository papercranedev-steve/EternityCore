/*
 * PartyBot Commander layer 3 (cerebellum): carries out the resurrection in combat the brain gave
 * this bot (TeamPlan::resurrections, Coordination/Duties/ResurrectAssigner.h: Rebirth on a dead
 * healer or tank). Assigned tier, value = the duty's net + the cast time's action, cost = that
 * action. BotActionType::Resurrect: TeamBotAI::CastResurrection finds the body or the corpse
 * and checks range and sight; nothing is cast out of range (no walk there in combat).
 */

#ifndef MANGOS_COMMANDER_RESURRECT_BEHAVIOR_H
#define MANGOS_COMMANDER_RESURRECT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class ResurrectBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "resurrect"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
