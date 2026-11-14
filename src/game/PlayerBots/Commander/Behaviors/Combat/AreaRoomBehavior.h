/*
 * PartyBot Commander layer 3: the area caster keeps its own room from the enemies running at
 * it ("aoe-room" module; doc .kiro/docs/2026-11-12-mage-hold.md 3.3.1, 3.4). Only for the bot the
 * brain names (AreaPlan::roomCaster); EscapeBehavior then only puts up its danger shield.
 *
 * User 2026-11-12: do not run or stop the Blizzard as soon as something comes; wait until it
 * is about to hit, by the caster's own danger, not by seconds; even take a hit or two so the
 * ones behind catch up and one Frost Nova holds them all. Only a crowd in the face stops at once.
 *
 * When (3.3.1): the enemies running at it (Perception/Forecast/Runners) on a time line: when
 * each gets within the Frost Nova's radius (Runners::ReachSec; the bot's running area channel
 * slows them) and when in melee (ArriveSec). Moments to act: now, and each of those, within
 * HORIZON_SEC. Acting at T is worth
 *   held(T)  the Frost Nova ready: the runners within its radius by T, each Value::HitLoss +
 *            HarassLoss over its hold (a root: at most EscapeBehavior::ROOT_HOLD_SEC)
 *   - wait(T) the hits until T of the ones in melee by then (meleeDps x time), less the
 *            absorbs (Ice Barrier: no health, no pushback), Value::Points
 * T is out when the caster would not survive EscapeBehavior::DANGER_SEC after it ((health +
 * absorbs - those hits) / their damage per second). The best T wins, a later one on a tie; it
 * acts when that is within ACT_SEC. A running cast or channel: CastBreakBenefit = worth(now) -
 * worth(at its end) when now is the best moment (CastBreak stops it when that beats what the
 * rest of it is worth; the death penalty when the hits until then kill it).
 * How (3.4, fixed order, not points):
 *   1. Frost Nova ready and every runner within its radius: Frost Nova (the area phase then
 *      places the next Blizzard on all of them from farther away, AreaSpot);
 *   2. Blink ready (ClassKit::Leap): turn, Blink (BotAction::facing). Landing: LeapSpots
 *      away from the runners; first one from which its ground channel still reaches the first
 *      runner (range - REACH_MARGIN_YD), then nearest the main tank (none: farthest from the
 *      runners). The area phase then puts the Blizzard ahead of the first runner (AreaPlace).
 *      Also while rooted (Blink frees it).
 *   3. Frost Nova ready and a runner within its radius: Frost Nova what it can; then, and
 *      when nothing else is left: to the tank (RunToTank).
 * Ready (2026-11-14, strat-20261114-180150: a rotation Fire Blast's global cooldown made the
 *   Frost Nova and Blink "not ready" 1.4 s before they were, and the mage walked to the tank
 *   under the hits): Frost Nova and Blink count with the seconds until they can be cast
 *   (PartyBotAI::SpellReadyInSec) when that is within a global cooldown (Value::GCD_SEC); a
 *   longer cooldown counts as not ready, as before (waiting for it is not weighed against the
 *   other steps). The Frost Nova's moment is one of the time line's moments; a step whose
 *   spell is that soon waits for it when the caster survives until then (the "When" danger
 *   check), else the next step.
 * Another round (user 2026-11-14, doc .kiro/docs/2026-11-14-area-round.md): where it would go
 *   to the tank ("Frost Nova did not hold them all", "no Frost Nova"), it stays when the area
 *   phase plans a new ground channel for it (AreaPlan::CastBy, not the running one) and
 *   Perception/Forecast/AreaRound says every chaser is under control and stays off it until the
 *   channel ends or its next rescue (Frost Nova, Blink, Cone of Cold, PartyBotAI::SpellReadyInSec)
 *   is ready; the area phase then walks it to its spot and casts. All of them rooted (nobody
 *   running): before 2026-11-14 it always left them to the area phase; now to the tank unless
 *   that round is safe. A walk to the tank already under way stops once the round is safe (the
 *   plan can come a refresh after the Frost Nova); nothing else is re-judged on the way.
 * Tier Urgent. While its own walk is under way: hold it; nobody running at it: stop it.
 * Values: Frost Nova benefit held(now); Blink the runners' hits and harassment while they
 * walk its distance again; cost LostAction(GCD) + ManaWorth(its mana).
 * Cone of Cold (ClassKit::SlowCone, doc 3.5): turned to the nearest runner not slowed yet
 *   within its radius (talents included), every other one in the cone (SpellMgr::GetSpellCone)
 *   slowed too. Before the Blink of step 2 on the ones about to hit (user 2026-11-12: some room
 *   first). Before the timing whenever the Frost Nova is spent (on cooldown, or some rooted on
 *   the bot): the ones still coming as they get near. Not before that: it would spread out the
 *   ones a single Frost Nova could hold. Benefit: their hits and harassment over the slow's
 *   share of its duration (at most ROOT_HOLD_SEC). Running to the tank: a jump turn
 *   (BotAction::jumpTurn, Movement/JumpCast.h), it keeps running; standing: it turns and casts
 *   (BotAction::facing). Also on the way to the tank (before the "keep walking" hold); while in
 *   the air nothing else is proposed.
 */

#ifndef MANGOS_COMMANDER_AREA_ROOM_BEHAVIOR_H
#define MANGOS_COMMANDER_AREA_ROOM_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class AreaRoomBehavior : public IBehavior
    {
    public:
        static constexpr float ACT_SEC = 0.3f;          // a bot tick and a refresh: "now"
        static constexpr float HORIZON_SEC = 8.0f;      // a Blizzard's length
        static constexpr float RADIUS_MARGIN_YD = 0.5f; // inside the Frost Nova's edge by this much
        static constexpr float REACH_MARGIN_YD = 3.0f;  // a Blink landing keeps the first runner this far inside the Blizzard's range

        char const* GetId() const override { return "aoe-room"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        float CastBreakBenefit(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               uint32 castLeftMs, char const*& reason) const override;
    };
}

#endif
