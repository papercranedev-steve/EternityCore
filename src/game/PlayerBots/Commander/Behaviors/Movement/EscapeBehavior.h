/*
 * PartyBot Commander layer 3: a healer or ranged damage dealer with a melee enemy on it gets
 * away to the tank ("escape" module).
 *
 * Before: the rotations ran 15 yd away (RunAwayFromTarget / FleePoint) after a Frost Nova on
 * every melee contact, and the position module stepped out of the chasing enemy's area again and
 * again. In strat-20261101-224637 a mage chased by a Skeletal Berserker and an out-of-mana
 * Skeletal Guardian walked ~100 yd away from the tank into the next pack and died.
 *
 * The rotations' own escapes (TeamBotAI::RunAwayFromTarget, the mage's Blink / Frost Nova /
 * run block, Psychic Scream, the druid's roots and run) are gone since 2026-11-14.
 *
 * Chasers: enemies that act, are not crowd controlled, have melee on (EnemyFacts::meleeEnabled),
 * hit this bot and stand within TRIGGER_GAP of it (edge distance). Not one the tank waits to
 * taunt (RescueDuty::waitUntilMs, RescueAssigner::TauntWaitSec): it is taunted before it gets
 * here and copies this bot's threat, so the bot keeps casting.
 * Danger (user, 2026-11-01: "many mobs, or a few hits kill it"):
 *   survive = (health + active absorbs) / Value::IncomingOn(bot), no heals; absorbs = Power
 *   Word: Shield, Ice Barrier, Mana Shield up to the mana left. Dangerous = survive < DANGER_SEC
 *   and survive < the time the slowest chaser needs to die (Value::EnemyTimeToDie). More
 *   chasers count through the incoming damage.
 *
 *  0. Dangerous and nothing absorbs: cast the class kit shield (ClassKit::ReactiveShields)
 *     that absorbs most, tier Urgent; benefit = Points(min(absorb, incoming x DANGER_SEC)),
 *     cost = LostAction(GCD). The mage rotation puts Ice Barrier up by itself when attacked;
 *     Mana Shield (2 mana per point absorbed) only comes from here (user, 2026-11-01: only
 *     when it is really dangerous; strat-20261101-235124 lost a third of its mana to it).
 *
 * Hold spells (HoldSpells: mage Frost Nova, druid Entangling Roots, paladin Hammer of Justice,
 * priest Shackle Undead on undead). Effect read from the spell: a stun holds for its duration, a
 * root (or control broken by damage) at most ROOT_HOLD_SEC; slows are not used. Self-area
 * spells take every free chaser in the radius, single target spells the chaser they save most
 * on (creature type checked). Since 2026-11-07 (user: a priest saving itself with Psychic
 * Scream when everything runs at it counts) an area fear around the bot from
 * ClassKit::Controls (Psychic Scream, Howl of Terror) competes with them, valued by
 * Perception/Values/ControlValue (add risk near idle packs, scatter, mana, cooldown included).
 *
 * Proposals (one per tick, besides Hold while a MoveTo of any module is under way):
 *  1. Hold spell, only in danger, castable, best value first. Tier Urgent.
 *       benefit = sum over held chasers: HitLoss + HarassLoss over the hold seconds
 *       cost    = LostAction(bot, max(GCD, cast time))
 *     Not worth it or nothing castable: straight to 2.
 *  2. Run to the tank: a tank (Perception/Tanks.h) that can act, farther than START_DISTANCE and
 *     at most MAX_RUN_DISTANCE away; destination STAND_OFF yd from it on the bot's side; the path
 *     must not pass an idle creature (SafeRoute::PathIsSafe), else FleePoint::Find (tank first,
 *     then party, then a ring; never into an idle pack). Tier Urgent in danger, else Assigned.
 *       horizon H = min(HORIZON_SEC, time for the chaser to die); walk = distance x PATH_FACTOR / speed
 *       benefit = sum over chasers: loss(H) - loss(min(H, walk + TAUNT_REACTION_SEC)), loss =
 *                 Value::MoveLoss(vs the tank) + Value::HarassLoss: the tank takes it on arrival
 *                 (its taunt is cheap then: RescueValue counts distance and harassment too)
 *       cost    = LostAction(bot, walk)
 *  3. No tank: only with every chaser rooted, run to FleePoint::Find (the old Nova-and-run);
 *     benefit = HitLoss + HarassLoss over ROOT_HOLD_SEC per chaser, cost = LostAction(walk).
 *     Otherwise nothing: stay and fight (running without anybody to take the enemy only kites
 *     it around the room).
 * holdMs = walk x 1000 + HOLD_EXTRA_MS.
 *
 *
 * Free shields (2026-11-02, strat-20261102-205906): hits taken by Ice Barrier / Power Word:
 * Shield cost no health and no spell pushback, so in steps 2 and 3 the losses start only after
 * absorbedSec = free absorb / IncomingOn(bot). A frost mage under Ice Barrier stays and casts
 * at a weak chaser (it ran 17 yd from a Skeletal Guardian hitting for ~50 and took 0 damage).
 * Mana Shield counts for the danger only (it costs mana per point).
 * Stopping: once nobody chases (the tank took it), the module's own run
 * (BotDirectives::GetMovementOwner == "escape") is stopped where the bot is
 * (BotActionType::StopMove); before, the mage walked on for ~3 s after the tank's taunt.
 *
 * Not while: the retreat runs, the pull is in Approach / Return, out of combat, rooted, cannot act.
 *
 * Weak chasers: no hold spell (not dangerous), and running is valued against the walk: little
 * damage on top of the tank and a few seconds to live give little benefit, so the bot stays
 * and fights (a frost mage under its Ice Barrier).
 *
 * Known limits: the tank's position is from the last refresh and it may move on; the tank is
 * assumed to take the enemy on arrival, and staying is valued as if nobody took it over within
 * the horizon (the tank's taunt from range is not predicted); no Blink; ranged chasers (casters standing back) are
 * left to the tank's own valuation; absorbed damage costs mana, not counted as a loss; hunters,
 * shamans and warlocks have no root / stun in their spell lists (slows and fears not used).
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
        static constexpr float DANGER_SEC = 8.0f;           // survive less than this (and less than the chasers): dangerous
        static constexpr float ROOT_HOLD_SEC = 4.0f;        // roots break on damage: counted shorter than their 8-10 s
        static constexpr float START_DISTANCE = 8.0f;       // run only when the tank is farther than this
        static constexpr float MAX_RUN_DISTANCE = 45.0f;
        static constexpr float STAND_OFF = 3.0f;            // stop this far from the tank
        static constexpr float MAX_PATH_DETOUR = 2.0f;
        static constexpr float HORIZON_SEC = 15.0f;
        static constexpr float TAUNT_REACTION_SEC = 1.0f;   // tank turns, taunts or hits after the arrival
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "escape"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
