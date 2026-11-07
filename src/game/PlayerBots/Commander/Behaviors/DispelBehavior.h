/*
 * PartyBot Commander layer 3: casts the dispels DispelAssigner gave this bot (2026-11-03).
 *
 * Takeover (Takeover::Dispel): in combat the rotation's own dispels (PartyBotAI::
 * CheckForDispelTargets) stand down; who removes what is decided by the brain
 * (Coordination/DispelAssigner.h, worth: Perception/DispelValue.h). Out of combat the
 * rotation dispels as before.
 * Live checks before proposing: the member is still there with the debuff, in line of sight
 * (not in the facts), and the spell is castable. Tier Urgent for control effects (the member
 * cannot act), else Assigned; benefit = the debuff's worth, cost = one global cooldown.
 * An urgent heal order (tier Urgent) beats an Assigned dispel in the arbiter, which replaces
 * the old "no non-control dispels while an urgent heal waits" rule (VetoChecks keeps it for
 * the rotation when this module is off).
 * Until 2026-11-03 this module only vetoed dispels of the rotation (reapplied debuffs, sleeps
 * that the next hit breaks, slows on members who need not move); those cases are now part of
 * the worth (DispelValue).
 */

#ifndef MANGOS_COMMANDER_DISPEL_BEHAVIOR_H
#define MANGOS_COMMANDER_DISPEL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class DispelBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "dispel"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        // The bot has a control dispel duty (the member cannot act).
        bool WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
    };
}

#endif
