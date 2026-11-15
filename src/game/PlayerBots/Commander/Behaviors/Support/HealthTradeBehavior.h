/*
 * PartyBot Commander layer 3 (cerebellum): the bot's own health as a resource ("health" module,
 * user 2026-11-15). Bots whose kit has ClassKit::HealthToMana (warlock Life Tap) or
 * ClassKit::DrainHeal (warlock Drain Life). Only this module casts them in combat; the rotation
 * does not.
 *
 * Healed: a healer bot that can act, has mana and is within reach would heal it
 * (SurvivalValue::HealOn > 0). Human healers are not counted (they may not heal), and even a
 * bot healer may not keep up (other roles may fail, ai-architecture rule): a healer only lets
 * the bot spend more of its health; Drain Life stays its own fallback with healers around.
 *
 * Life Tap, in combat:
 *   gates    the mana it gives fits (nothing wasted); mana below TAP_MANA_PCT (healed: keep
 *            it topped, the healer pays the health back) / TAP_MANA_ALONE_PCT (alone: every tap
 *            is drained back later); health after the tap at least TAP_FLOOR_PCT (healed),
 *            alone DRAIN_BELOW_ALONE_PCT (a tap does not send it into draining), or
 *            TAP_FLOOR_ALONE_PCT once its mana does not pay one filler (dry: tap, then drain);
 *            not in danger (SurvivalValue::Of); alone not while hit unless dry.
 *   benefit  LostAction(seconds of filler casting the mana pays for); alone capped at the fight
 *            left (only when the mana would not last it)
 *   cost     LostAction(GCD). The health is gated by the floors above, not priced.
 * Drain Life, in combat, on its victim:
 *   gates    health below DRAIN_BELOW_PCT (healed) / DRAIN_BELOW_ALONE_PCT (alone).
 *   benefit  Points(the health it drains before the channel ends or the victim dies
 *            (Value::EnemyDyingSec), capped at the missing health)
 *   cost     LostAction(channel) x (1 - its damage per second / the filler's)
 *            + ManaWorth::Points(its mana)
 * Alone the two make the warlock's cycle: tap while health is high, drain it back below
 * DRAIN_BELOW_ALONE_PCT, Shadow Bolts in between. Tier Routine: the rotation's damage spells
 * otherwise.
 */

#ifndef MANGOS_COMMANDER_HEALTH_TRADE_BEHAVIOR_H
#define MANGOS_COMMANDER_HEALTH_TRADE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class HealthTradeBehavior : public IBehavior
    {
    public:
        // Untested guesses (user 2026-11-15: no healer -> no blind tapping, drain instead).
        static constexpr float TAP_MANA_PCT = 80.0f;
        static constexpr float TAP_MANA_ALONE_PCT = 40.0f;
        static constexpr float TAP_FLOOR_PCT = 45.0f;
        static constexpr float TAP_FLOOR_ALONE_PCT = 50.0f;
        static constexpr float DRAIN_BELOW_PCT = 35.0f;
        static constexpr float DRAIN_BELOW_ALONE_PCT = 70.0f;

        char const* GetId() const override { return "health"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
