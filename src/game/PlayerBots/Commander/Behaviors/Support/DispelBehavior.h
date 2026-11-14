/*
 * PartyBot Commander layer 3: casts the dispels DispelAssigner gave this bot (2026-11-03).
 *
 * In combat only this module dispels (the rotation's PartyBotAI::CheckForDispelTargets runs
 * out of combat only); who removes what is decided by the brain
 * (Coordination/Duties/DispelAssigner.h, worth: Perception/Values/DispelValue.h).
 * Live checks before proposing: the member is still there with the debuff, in line of sight
 * (not in the facts), and the spell is castable. Tier Urgent for control effects (the member
 * cannot act), else Assigned; benefit = the debuff's worth, cost = one global cooldown.
 * An urgent heal order (tier Urgent) beats an Assigned dispel in the arbiter, which replaces
 * the old "no non-control dispels while an urgent heal waits" rule (VetoChecks keeps it for
 * the rotation's out of combat dispels).
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
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        // The bot has a control dispel duty (the member cannot act).
        bool WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
    };
}

#endif
