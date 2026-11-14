/*
 * PartyBot Commander layer 1 (eyes): whether a damage cooldown (PartyMemberFacts::bursts) may go
 * now, and what it is worth in points. Facts and consequences only; BurstBehavior proposes it.
 * Cooldown doc .kiro/docs/2026-11-14-cooldowns.md section 6, user 2026-11-14: short cooldowns
 * (Rapid Fire, the rogue's) go whenever the moment is right.
 *
 * Gates (all must hold; `why` names the first that fails):
 *   ready      off cooldown, the power, can act (BurstAbility::ready); a victim that is alive
 *   threat     the victim is the bot's, nobody holds it, the doctrine stamped it NO_HOLD, or a
 *              holder (Tanks::IsHolder) is ahead of the bot by ThreatRoom::Enter x (1 + damageMult):
 *              the burst raises its threat rate (tanks: no gate)
 *   lasts      the fight goes on for MIN_LAST_SHARE of its duration (Value::FightRemainingSec):
 *              not on the last enemy about to die
 *   long       a cooldown longer than SHORT_COOLDOWN_SEC (Recklessness, Retaliation: 30 min) only
 *              in a big fight (BigFight: a boss is fighting us, or a member is about to die,
 *              SurvivalValue::Danger), and the fight goes on for LONG_MIN_FIGHT_SEC unless
 *              someone is about to die (user 2026-11-14)
 *   own        notTank: not a tank; healTakenCut: nothing hits it; minManaPct; minEnemiesNear:
 *              acting enemies within NEAR_YD of the bot (the splash: Blade Flurry, Sweeping
 *              Strikes); minAttackersOnSelf: melee enemies on it within NEAR_YD; needsPet: its
 *              pet alive and on the victim; needsCombo: 5 combo points on the victim
 * Pairings (thenSpellId): the cast it boosts is checked live by BurstBehavior (castable now; for
 * thenNotOnTarget the bot's aura of it not on the victim).
 *
 * Value:
 *   benefit = Value::ContributionPerSec(member) x (damageMult x min(duration, fight left) + extraSec)
 *   cost    = LostAction(GCD) when it starts the global cooldown
 *             + Points(damage taken x damageTakenUp x duration) (Recklessness)
 * No cooldown points: short cooldowns are back for the next pull, long ones are held by the gate.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_BURST_VALUE_H
#define MANGOS_COMMANDER_BURST_VALUE_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace BurstValue
    {
        // Untested guesses.
        static constexpr float SHORT_COOLDOWN_SEC = 300.0f;     // user 2026-11-14: 5 min
        static constexpr float LONG_MIN_FIGHT_SEC = 30.0f;
        static constexpr float MIN_LAST_SHARE = 0.5f;
        static constexpr float NEAR_YD = 8.0f;
        static constexpr uint8 FULL_COMBO = 5;

        struct Option
        {
            bool ok = false;
            char const* why = "";       // static: the gate that failed, or what it is for
            float benefit = 0.0f;
            float cost = 0.0f;
        };

        // A boss is fighting the party, or a member is about to die.
        bool BigFight(WorldModel const& world);

        // `victim`: the bot's victim (null = none). `noHold`: the doctrine's STAMP_NO_HOLD on it.
        Option Of(WorldModel const& world, PartyMemberFacts const& member, BurstAbility const& a,
                  EnemyFacts const* victim, bool noHold, float tickSec);
    }
}

#endif
