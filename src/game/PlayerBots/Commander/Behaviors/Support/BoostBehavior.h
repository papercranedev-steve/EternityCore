/*
 * PartyBot Commander layer 3 (cerebellum): casts the cooldown the brain gave this bot for a party
 * member ("boost" module, TeamPlan::boosts, Coordination/Duties/BoostAssigner.h): Innervate, Power
 * Infusion on the target, Mana Tide Totem on itself. Assigned tier, value = the duty's net + one
 * global cooldown, cost = that global cooldown (as the rescue behavior). The target must be in
 * line of sight and the spell castable (CanTryToCastSpell: range, cooldown, mana).
 * Takes over Takeover::Boost: the rotations' own Innervate (druid, on itself below 10% mana)
 * and Mana Tide Totem (below 50% of its own mana) stand down (PartyBotAI::IsBoostControlled).
 */

#ifndef MANGOS_COMMANDER_BOOST_BEHAVIOR_H
#define MANGOS_COMMANDER_BOOST_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class BoostBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "boost"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
