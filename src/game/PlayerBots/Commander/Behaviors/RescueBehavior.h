/*
 * PartyBot Commander layer 3: carries out the rescue the brain gave this bot (RescueAssigner,
 * todo item 20, .kiro/docs/2026-11-07/2026-11-07-team-rescue.md).
 *
 * Takeovers: Takeover::Taunt (tank bots with a taunt: the shared tank step's own taunts in
 * PartyBotAI::UpdateInCombatAI stand down) and Takeover::ThreatDrop (non-tank bots with a
 * threat drop: the priest rotation's "attacked: Fade"). Taunts and Fade are only cast for a
 * rescue duty now (they were ThreatBehavior's own choice until 2026-11-07).
 *
 * Propose, for this bot's RescueDuty (TeamPlan::FindRescueFor), checked live:
 *   Taunt       the enemy alive, in sight, CanCastWithStance (range, cooldown, rage, stance
 *               swap; the arbiter swaps stance first). Out of range or sight: nothing here,
 *               TargetAssigner gives the tank the enemy and it walks over (TankTargets.cpp).
 *   ThreatDrop  CanTryToCastSpell on itself.
 *   Threat      nothing here: TargetBehavior attacks it.
 * Tier Urgent when the duty is (the victim dies within the horizon), else Assigned;
 * value = the duty's net + one global cooldown, cost = that global cooldown (as the crowd
 * control behavior: the arbiter compares by net).
 * WantsTickNow while the bot has any rescue duty (for Threat the target switch happens on that
 * tick): the bot reacts at the refresh that saw the loose enemy (was ThreatBehavior's wake-up
 * on "a non-tank is hit").
 */

#ifndef MANGOS_COMMANDER_RESCUE_BEHAVIOR_H
#define MANGOS_COMMANDER_RESCUE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class RescueBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "rescue"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        bool WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
    };
}

#endif
