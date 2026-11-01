/*
 * PartyBot Commander layer 3: whom a healer bot heals, when, and with which rank ("heal" module).
 * Design: .kiro/docs/2026-11-01-heal-plan.md. Arithmetic: Behaviors/HealPlan.h.
 *
 * Healer bots in combat with a party enemy. While the module runs, the rotation's own heal
 * choice is off (PartyBotAI::FindAndHealInjuredAlly / FindAndPreHealTarget): at its heal step
 * the rotation casts this module's HealOrder of the tick, or nothing (rest). Shields, Fade,
 * dispels and the rest of the rotation keep their order; while the order is an emergency
 * (Urgent) only control effects are dispelled before it (VetoChecks::IsDispelVetoed). ThreatBehavior's healer branch and
 * ManaBudgetBehavior's heal veto stand down; aggro is checked here (ThreatBehavior::HealPullLoss)
 * and mana through HealPlan::ManaWeight.
 *
 * Rhythm (user, 2026-11-01): wait until a target misses a deficit the healer can lift quickly,
 * chain heals until it is topped up, then rest (no heal, no HoT) so the five second rule ends and
 * spirit regeneration runs. HoTs are not used in combat (worse per mana than Heal ranks with
 * gear, and each one restarts the five second rule); hybrid heals count their direct part plus
 * HealSpells::HOT_INCOMING_SHARE of the expected damage for the HoT part.
 *
 * Targets (alive, healable from here: line of sight, 30 yd), in this order:
 *  1. emergencies (being hit and at the reserve or dying within HealPlan::EMERGENCY_TTD_SEC),
 *     soonest death first;
 *  2. lifts and the target of a running chain, by time until they reach their reserve (members
 *     nobody hits come last; the healer itself is one of them when nothing hits it), then the
 *     chain, then health percent. Pets after players. (Until 2026-11-02 the chain came before
 *     every lift: strat-20261102-012937.)
 * The first target with a usable heal gets the order. Emergencies are Urgent and take a heal
 * that pulls aggro only when no other lands in time. Below ThreatBehavior::HEAL_IGNORE_THREAT_PCT
 * aggro is not checked.
 *
 * Inputs: Value::TakenDps (measured damage or the enemy melee prediction, whichever is larger),
 * PartyMemberFacts::takenBurst for the reserve, mana regeneration and the ManaBudget tension.
 */

#ifndef MANGOS_COMMANDER_HEAL_PLAN_BEHAVIOR_H
#define MANGOS_COMMANDER_HEAL_PLAN_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class HealPlanBehavior : public IBehavior
    {
    public:
        // A chain goes on when the next heal starts within the bot interval + this after the
        // previous one ended.
        static constexpr uint32 CHAIN_GAP_MS = 500;

        char const* GetId() const override { return "heal"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool ControlsHealing(PartyBotAI const* bot, WorldModel const& world) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // The module is on and decides this healer's heals now (other modules check this).
        static bool ControlsHealingOf(PartyBotAI const* bot, WorldModel const& world);
    };
}

#endif
