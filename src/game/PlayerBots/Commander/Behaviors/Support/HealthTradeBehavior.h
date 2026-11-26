/*
 * PartyBot Commander layer 3 (cerebellum): the bot's own health as a resource ("health" module,
 * user 2026-11-15). Bots whose kit has ClassKit::HealthToMana (warlock Life Tap) or
 * ClassKit::DrainHeal (warlock Drain Life). Only this module casts them in combat; the rotation
 * does not.
 *
 * The warlock pays for itself (user 2026-11-16): out of mana it taps, the last mana goes into a
 * Drain Life, the health it drains back is tapped into Shadow Bolts again. Healers are not
 * looked at (they may not heal; whether a healer spends its mana on the warlock is the
 * healer's call, ManaBudgetBehavior). Life Tap cannot kill (it fizzles at or below its cost,
 * spell_warlock.cpp), so there is no health floor: only nothing may be hurting it.
 *
 * Safe: no acting enemy on it or casting at it, no damage taken within DamageIntake's window
 * (damage over time and area spells included; Life Tap's own health is not damage). Taking
 * aggro is ThreatBehavior's (it stops the casts before an overtake).
 *
 * Cycle on (CycleOf): the mana and every tap its health still gives, (M + H x k) / S, do not
 * last the fight (Value::FightRemainingSec). S = filler mana per second - regeneration while
 * casting, k = Life Tap mana per health.
 *
 * Life Tap, in combat:
 *   gates    the mana it gives fits (nothing wasted); health above its cost; safe; not in
 *            danger (SurvivalValue::Of); the mana it has does not last the fight (benefit > 0;
 *            user 2026-11-16: tap when the mana runs out, not to keep it topped).
 *   when     just in time (user 2026-11-26; strat-20261126-232120: seven taps in a row to full
 *            mana and 51 health, no Shadow Bolt; the rotation is not a candidate, so any tap
 *            with a positive score pushed it out): the mana left does not pay the next filler,
 *            or the global cooldown is free anyway (walking; no victim, or it is out of the
 *            filler's sight or range). A tap now gives what a tap later gives.
 *   benefit  LostAction(min(T, (M + gain) / S) - min(T, M / S)): filler seconds it adds
 *   cost     LostAction(GCD)
 * Drain Life, in combat, on its victim:
 *   cycle    the cycle is on, its mana pays less than the drain and one filler (the last mana
 *            goes into the drain, not into a Shadow Bolt that leaves too little for one), and
 *            the whole drain fits in the missing health, and its health cannot pay a tap now
 *            (at or below the tap's cost, or not safe / in danger: with taps just in time the
 *            mana stays low all fight, the drain would replace the Shadow Bolts). Hit or not.
 *            benefit  LostAction(channel x its damage share of the filler's)
 *                     + LostAction((drained health x k - its mana) / S): what the health buys back
 *            cost     LostAction(channel) x WAND_SHARE: near dry the wand is what it displaces
 *   own heal health below DRAIN_BELOW_PCT while it takes damage.
 *            benefit  Points(the health it drains before the channel ends or the victim dies
 *                     (Value::EnemyDyingSec), capped at the missing health)
 *            cost     LostAction(channel) x (1 - its damage share) + ManaWorth::Points(its mana)
 * Tier Routine: the rotation's damage spells otherwise.
 */

#ifndef MANGOS_COMMANDER_HEALTH_TRADE_BEHAVIOR_H
#define MANGOS_COMMANDER_HEALTH_TRADE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    struct PartyMemberFacts;

    class HealthTradeBehavior : public IBehavior
    {
    public:
        // Untested guesses: its own heal under fire below this health; a wand's damage per
        // second as a share of the filler's.
        static constexpr float DRAIN_BELOW_PCT = 50.0f;
        static constexpr float WAND_SHARE = 0.3f;

        struct Cycle
        {
            bool on = false;            // mana + tappable health do not last the fight (header)
            float spend = 0.0f;         // S
            float fillerCost = 0.0f;    // mana of one filler cast
        };
        // Map thread (reads the bot's spell costs).
        static Cycle CycleOf(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self);

        char const* GetId() const override { return "health"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
