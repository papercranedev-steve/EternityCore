/*
 * PartyBot Commander layer 3: a healer or ranged damage dealer with a melee enemy on it gets
 * away to the tank ("escape" module).
 *
 * Before: the rotations ran 15 yd away (RunAwayFromTarget / FleePoint) after a Frost Nova on
 * every melee contact, and the position module stepped out of the chasing enemy's area again and
 * again. In strat-20261101-224637 a mage chased by a Skeletal Berserker and an out-of-mana
 * Skeletal Guardian walked ~100 yd away from the tank into the next pack and died.
 *
 * While this module is on, PartyBotAI::RunAwayFromTarget and the mage's Blink / Frost Nova /
 * run block stand down (ControlsEscape -> BotDirectives::IsEscapeControlled).
 *
 * Chasers: enemies that act, are not crowd controlled, have melee on (EnemyFacts::meleeEnabled),
 * hit this bot and stand within TRIGGER_GAP of it (edge distance).
 * Danger: the bot survives everything hitting it less than DANGER_SEC (Value::IncomingOn, no
 * heals: "a few hits kill it"), or at least DANGER_CHASERS chasers (user, 2026-11-01).
 *
 * Proposals (one per tick, besides Hold while a MoveTo of any module is under way):
 *  1. Root spell (mage Frost Nova; RootSpell() is the place for other classes), only in danger,
 *     castable, a chaser inside its radius and not rooted yet. Tier Urgent.
 *       benefit = sum over chasers in the radius: HitLoss + HarassLoss over ROOT_HOLD_SEC
 *       cost    = LostAction(bot, GCD)
 *  2. Run to the tank: a tank (Perception/Tanks.h) that can act, farther than START_DISTANCE and
 *     at most MAX_RUN_DISTANCE away; destination STAND_OFF yd from it on the bot's side; the path
 *     must not pass an idle creature (SafeRoute::PathIsSafe), else FleePoint::Find (tank first,
 *     then party, then a ring; never into an idle pack). Tier Urgent in danger, else Assigned.
 *       horizon H = min(HORIZON_SEC, time for the chaser to die); walk = distance x PATH_FACTOR / speed
 *       benefit = sum over chasers: loss(H) - loss(min(H, walk + TAUNT_REACTION_SEC)), loss =
 *                 Value::MoveLoss(vs the tank) + Value::HarassLoss: the tank takes it on arrival
 *                 (its taunt is cheap then: ThreatBehavior counts distance and harassment too)
 *       cost    = LostAction(bot, walk)
 *  3. No tank: only with every chaser rooted, run to FleePoint::Find (the old Nova-and-run);
 *     benefit = HitLoss + HarassLoss over ROOT_HOLD_SEC per chaser, cost = LostAction(walk).
 *     Otherwise nothing: stay and fight (running without anybody to take the enemy only kites
 *     it around the room).
 * holdMs = walk x 1000 + HOLD_EXTRA_MS.
 *
 * Not while: the retreat runs, the pull is in Approach / Return, out of combat, rooted, cannot act.
 *
 * Known limits: the tank's position is from the last refresh and it may move on; the tank is
 * assumed to take the enemy on arrival; no Blink; ranged chasers (casters standing back) are
 * left to the tank's own valuation.
 */

#ifndef MANGOS_COMMANDER_ESCAPE_BEHAVIOR_H
#define MANGOS_COMMANDER_ESCAPE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class EscapeBehavior : public IBehavior
    {
    public:
        static constexpr float TRIGGER_GAP = 8.0f;          // chaser edge within this of the bot
        static constexpr float DANGER_SEC = 6.0f;           // survive less than this: dangerous
        static constexpr uint32 DANGER_CHASERS = 2;
        static constexpr float ROOT_HOLD_SEC = 4.0f;        // Frost Nova breaks on damage: counted shorter than 8 s
        static constexpr float START_DISTANCE = 8.0f;       // run only when the tank is farther than this
        static constexpr float MAX_RUN_DISTANCE = 45.0f;
        static constexpr float STAND_OFF = 3.0f;            // stop this far from the tank
        static constexpr float MAX_PATH_DETOUR = 2.0f;
        static constexpr float HORIZON_SEC = 15.0f;
        static constexpr float TAUNT_REACTION_SEC = 1.0f;   // tank turns, taunts or hits after the arrival
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "escape"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool ControlsEscape(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
