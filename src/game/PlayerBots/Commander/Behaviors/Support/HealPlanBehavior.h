/*
 * PartyBot Commander layer 3: whom a healer bot heals, when, and with which rank ("heal" module).
 * Design: .kiro/docs/2026-11-01-heal-plan.md. Arithmetic: Behaviors/Support/HealPlan.h.
 *
 * Healer bots while the party fights (a party enemy), also before the healer is in combat
 * itself (2026-11-07; before, an out of combat healer healed with the original out of combat
 * rotation, past the pull's healing hold). Out of combat the order is cast after the Commander
 * hook (PartyBotAI::UpdateAI); while the pull holds healing (PullPlan::HoldsWaiting) only
 * emergencies. While the module runs, the rotation's own heal
 * choice is off (TeamBotAI::FindAndHealInjuredAlly / FindAndPreHealTarget): at its heal step
 * the rotation casts this module's HealOrder of the tick, or nothing (rest). Fade (RescueBehavior),
 * dispels and the rest of the rotation keep their order; while the order is an emergency
 * (Urgent) only control effects are dispelled before it (VetoChecks::IsDispelVetoed). ThreatBehavior's healer branch and
 * ManaBudgetBehavior's heal veto stand down; aggro is checked here (ThreatBehavior::HealPullLoss)
 * and mana through HealPlan::ManaWeight.
 *
 * Rhythm (user, 2026-11-01): wait until a target misses a deficit the healer can lift quickly,
 * chain heals until it is topped up, then rest (no heal, no HoT) so the five second rule ends and
 * spirit regeneration runs. Hybrid heals count their direct part plus
 * HealSpells::HOT_INCOMING_SHARE of the expected damage for the HoT part. The class kit says
 * which heals are used (ClassKit::CombatHealUse, user 2026-11-15): priest Renew never (until
 * then no HoT was used in combat), druid Regrowth and paladin Holy Shock (instant, valued by
 * the heal it triggers: HealSpells::HealingPart; 2026-11-15) only in an emergency. Chain Heal
 * counts its jumps on the others too (ChainHealForecast, HealPlan::Option::otherHeal; 2026-11-15). The target's intake is
 * the damage the HoTs on it do not cover (PartyMemberFacts::hots).
 *
 * HoTs and Swiftmend (Behaviors/Support/HotPlan.h, user 2026-11-15): the healer's HoT
 * (Rejuvenation) on a member being hit that has none of its own, the rank by HotPlan::PickHot;
 * before a lift's heal on that target, else when nobody needs a lift (most damage taken first).
 * Swiftmend: in an emergency first; else right before the eaten HoT's last tick when the
 * target misses its heal (or with two ticks left instead of a lift's cast). The lift of a target
 * whose HoT is about to be eaten waits for that deficit. A pure HoT starts no heal chain.
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
 * Power Word: Shield (2026-11-03, taken over from the rotation with the heals): ordered instead
 * of the picked heal when the target is the healer itself, being hit below SHIELD_SELF_PCT, and
 * the heal has a cast time (no pushback under the shield). Not on Weakened Soul or an active
 * shield. Benefit = the hits it absorbs over its duration; cost = absorb left unused. The
 * rotation's own shield casts stand down. A shield for someone about to die is the team
 * rescue's (2026-11-07: RescueAssigner compares it with Shield Wall, Lay on Hands and the rest,
 * so a priest does not shield a tank that walls); until then this module also shielded an
 * emergency its heal would not reach in time.
 *
 * Heal boosts (ClassKit::HealBoosts, cooldown doc .kiro/docs/2026-11-14-cooldowns.md 7.1,
 * user 2026-11-14): Inner Focus and Divine Favor wait for a deficit one big heal fills
 * (HealPlan::PickBoosted), then go on right before that heal (BotAction::prepSpell). While
 * one is ready the lifts themselves wait for that deficit (Target::boostDeficit; Divine Favor
 * too since 2026-11-15, its crit counted: x HealPlan::CRIT_MULT), on targets whose safe deficit
 * holds it, so the gap comes;
 * Nature's Swiftness makes the biggest heal instant in an emergency the picked heal would not
 * reach in time. The rotations' own Inner Focus and Divine Favor stand down while this module
 * decides heals.
 *
 * Free self heal (ClassKit::FreeSelfHeal, Desperate Prayer; user 2026-11-14): when the bot is hit
 * and misses at least its amount x (1 - OVERHEAL_MAX), it is cast first (a Replace proposal),
 * unless another member needs an emergency heal. Also for bots that are not healers (shadow
 * priests: ControlsSelfHealOf); nothing else of this module runs for them. Not among the
 * heals Pick chooses from.
 *
 * Steady healers (ClassKit::HealsSteadily, paladin; user 2026-11-16): no lift + rest rhythm, a
 * heal starts once the efficient one lands on a deficit (full targets being hit included), the
 * rank by mana per health; with Divine Favor ready a target being hit is held for the boosted
 * big heal (HealPlan.h); CastWatch cancels one about to land as overheal (HealPlan.h).
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
        // The healer shields itself before a cast-time heal when hit below this (no pushback).
        static constexpr float SHIELD_SELF_PCT = 50.0f;
        // A pet that holds no enemy gets no HoT from this mana weight on (HealPlan::ManaWeight,
        // 0 plentiful .. 1 scarce).
        static constexpr float PET_HOT_MAX_MANA_WEIGHT = 0.5f;

        char const* GetId() const override { return "heal"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        bool TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // The module is on and decides this healer's heals now (other modules check this).
        static bool ControlsHealingOf(TeamBotAI const* bot, WorldModel const& world);
        // The module is on and decides this bot's free self heal now (any role).
        static bool ControlsSelfHealOf(TeamBotAI const* bot, WorldModel const& world);
    };
}

#endif
