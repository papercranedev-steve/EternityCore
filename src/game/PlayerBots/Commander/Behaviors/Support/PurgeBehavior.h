/*
 * PartyBot Commander layer 3 (cerebellum): casts the bot's purge duty (TeamPlan::purges,
 * PurgeAssigner.h; boss doc G11) on the enemy while the buff is on it: Assigned tier, value =
 * the duty's points, cost = the global cooldown.
 */

#ifndef MANGOS_COMMANDER_PURGE_BEHAVIOR_H
#define MANGOS_COMMANDER_PURGE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PurgeBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "purge"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
