/*
 * PartyBot Commander layer 3: carries out the rescue the brain gave this bot (RescueAssigner,
 * todo item 20, .kiro/docs/2026-11-07/2026-11-07-team-rescue.md).
 *
 * Taunts, threat drops and survival spells (Divine Shield, Ice Block, Shield Wall, Vanish,
 * Evasion, Barkskin...) are only cast for a rescue duty: the rotations' own uses are gone
 * (2026-11-14), and so is ThreatBehavior's own choice of taunts and threat drops (2026-11-07).
 *
 * Propose, for this bot's RescueDuty (TeamPlan::FindRescueFor), checked live:
 *   Taunt       the enemy alive, in sight, CanCastWithStance (range, cooldown, rage, stance
 *               swap; the arbiter swaps stance first). Out of range or sight: nothing here,
 *               TargetAssigner gives the tank the enemy and it walks over (TankTargets.cpp).
 *               A waiting taunt (RescueDuty::waitUntilMs: the enemy runs at a ranged victim
 *               who keeps adding threat the taunt copies): nothing until it is due, i.e. the
 *               time is up, or live within RescueAssigner::WAIT_LEAD_SEC the enemy is in melee
 *               reach of its victim or out of the tank's reach (it outruns the tank), or it
 *               turned to someone else.
 *   ThreatDrop  CanTryToCastSpell on itself (Fade, Feign Death, Vanish), or on the enemy in
 *               sight (ThreatDropAbility::onTarget: Feint, Cower).
 *   Threat      nothing here: TargetBehavior attacks it.
 * A Save or ThreatDrop spell on cooldown whose reset is ready (CooldownResetFacts: Cold Snap
 * before Ice Block, Preparation before Vanish / Evasion; user 2026-11-15): the reset goes on
 * first, in the same cast (BotAction::prepResets).
 * The bot's pet's own duty (its facts: Shell Shield, Cower; user 2026-11-15, the pet as an
 * off-tank): the pet casts it (BotActionType::PetCast, besides the bot's own action).
 * Tier Urgent when the duty is (the victim dies within the horizon), else Assigned;
 * value = the duty's net + one global cooldown, cost = that global cooldown (as the crowd
 * control behavior: the arbiter compares by net); 0 for a spell off the global cooldown
 * (Taunt, Growl, Vanish).
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
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        bool WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;

    private:
        void ProposePet(PartyBotAI const* bot, WorldModel const& world,
                        TeamPlan const& plan, std::vector<Proposal>& out) const;
    };
}

#endif
