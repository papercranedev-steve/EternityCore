/*
 * PartyBot Commander layer 2 (brain), situation step: the area phase (.kiro/docs/2026-11-08-aoe-phase.md).
 * Before 2026-11-08 each damage dealer bot decided its own area spells in the cerebellum
 * (AreaDamageBehavior): a mage opened Blizzard 0.8 s after the tank's line of sight hide
 * ended (three enemies the tank had not touched), the tank was sent off a patrol's path in the
 * middle of the channel and seven enemies came to the mage (strat-20261108-161025). Now the
 * brain decides who, which spell, where and what first; AreaDamageBehavior only carries it out.
 *
 * Not while: no fight, a retreat, the pull's Approach / Return, Settle while the damage
 * dealers hold or the tank waits for the pack or hides from casters (PullPlan).
 * Tanks sent elsewhere (TeamPlan::fightSpot moving: the tank takes the pile to another spot;
 * read as FightSpotAssigner left it this refresh) take nothing back: AggroTurnCost::Options::busyTanks.
 *
 * 1. A bot channelling an area spell (PartyMemberFacts::areaChannel): the phase is its channel
 *    (step Channel), valued by AreaValue on the ticks left (paid). endCost = its net now -
 *    its net with every tank busy (what sending the tank away costs, Stragglers pays it).
 * 2. Otherwise every damage dealer bot, every ready area spell (AreaSpellFacts), every centre
 *    (ground spell: each acting enemy within range; else around the caster), valued by
 *    AreaValue for the whole spell (never "start and stop after a few ticks"):
 *      here   cast from where it stands (in range);
 *      spot   ground channels: AreaPlace.h (doc .kiro/docs/2026-11-12-mage-hold.md 3.2). Nothing
 *             runs at the caster: the best place of the position field (AreaSpot.h) and the
 *             centre moved to the near side of what it covers; its net - the walk. Enemies
 *             run at it: one candidate, ahead of the first of them, from here, only when the
 *             ticks before it arrives pay. The cerebellum checks sight and path to
 *             AreaPlan::spots in order and casts from where it stands when none is usable
 *             (netHere); the spell goes to the ground point AreaPlan::centrePos;
 *      shield its own shield first (SelfShield, Ice Barrier) whenever it is ready and not up
 *             (user 2026-11-12, doc .kiro/docs/2026-11-12-mage-hold.md 3.6: on cooldown
 *             before every area spell); the value is with the absorb, the score adds
 *             max(0, net with - net without - LostAction(GCD) - ManaWorth(its mana)).
 *    The best positive net wins; the one of the last refresh gets STICKY_POINTS.
 * 3. Or it waits (2026-11-12, doc .kiro/docs/2026-11-11-aoe-threat.md 4.3 / 4.4; AreaPlan::wait,
 *    the phase is not active): when starting later is worth more than now, a wait of `sec`
 *    costing net(later) x sec / the spell's length:
 *      Tank    every enemy it would turn is on the main tank: later nobody turns (net + turn
 *              loss), sec = their ThreatRunway shortfalls through the spell / ThreatGap::
 *              FillRate (the tank goes for them: TankTargets values the same runways, ThreatRisk).
 *              Not when their total did not shrink for TANK_STALL_MS.
 *      Shield  its shield is on cooldown: later with it (as the shield step), sec = the cooldown.
 *    Meanwhile its spot is the prep spot below (it walks there while it waits).
 * While the pull holds the party (the "not while" above, not a retreat), the
 * caster gets ready (AreaPlan::prep*; doc 4.4, 4.5, 8, user 2026-11-12: stand there first, a
 * turn predicted is too late):
 *   who     under the Area tactic the caster it counts on (DoctrinePlan::areaThreat); before
 *           the fight (no tactic yet) the area dealers when the pull expects at least
 *           TacticalDoctrine::AREA_MIN enemies; the most threat per second (AreaIntent);
 *   centre  the enemy on the main tank nearest it, else the planned fight spot (PullPlan::
 *           PlannedFightSpot);
 *   spot    AreaSpot for that centre (AreaValue only with enemies there);
 *   shield  whenever it is ready and not up (doc mage-hold 3.6), worth the health it takes
 *           (before the fight too).
 * Both: a healer bot's Power Word: Shield (SurvivalAbility Absorb, not self only, ready, in
 *   range, the caster without Weakened Soul) when net with it - net without > LostAction(healer,
 *   GCD) + ManaWorth(healer, mana) (user 2026-11-08: the party protects the caster).
 *   tauntReserve = net - net without the tanks' taunts (RescueAssigner charges it to a taunt
 *   spent on an enemy outside the area).
 * Room (AreaPlan::roomCaster, doc .kiro/docs/2026-11-12-mage-hold.md 3.3.1, 3.4): the area
 *   caster keeps its own room from what runs at it (AreaRoomBehavior): the caster of the phase,
 *   else the one waiting or getting ready, else the last refresh's while an enemy is on it,
 *   else the one the tactic counts on; a bot that can hold enemies around itself (a flee stop
 *   with a radius: Frost Nova).
 * Reservation (AreaPlan::reserved, 2026-11-13, a 预约: the condition is checked every
 *   refresh, the caster gets ready now): the caster of a phase to start (Move, Shield, Cast),
 *   of a wait or of the prep; when this refresh names none, the last one for RESERVE_HOLD_MS
 *   while it can act and has an area spell ready. Not during a channel (that is the phase).
 *   It starts no spell with a cast or channel time but its area spells (TeamPlan::
 *   ForbidsLongCast): the step is re-judged every refresh and dropped for one or a few now and
 *   then; a 2.5 s Frostbolt the rotation began in such a gap went on while the brain wanted
 *   the Blizzard (a cast is never re-judged), twice in strat-20261113-235712 (4.6 s).
 * Calculation trace: scope "area" (label = what came of it, data = the plan and the
 *   reservation), one "areacand" per step 2 candidate with its value or why it has none.
 * One area phase per party at a time.
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_AREA_PHASE_H
#define MANGOS_COMMANDER_AREA_PHASE_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct AreaPlan;

    class AreaPhaseAssigner : public IAssigner
    {
    public:
        static constexpr float STICKY_POINTS = 1.0f;
        // The Tank wait gives up when the total shortfall did not shrink by TANK_PROGRESS_MIN
        // threat for TANK_STALL_MS (no rage, nothing reaches, someone keeps adding threat).
        static constexpr uint32 TANK_STALL_MS = 3000;
        static constexpr float TANK_PROGRESS_MIN = 1.0f;
        // Reservation (header): kept this long after the last refresh that named the caster
        // (one global cooldown: a refresh or a few without a phase do not free it).
        static constexpr uint32 RESERVE_HOLD_MS = 1500;

        // Same id as AreaDamageBehavior: `.partybot ai module aoe off` switches both.
        char const* GetId() const override { return "aoe"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

    private:
        // Steps 1-3 and the prep (header); `prev` = the last refresh's plan. Returns what came
        // of it (the calculation trace's label).
        static char const* Plan(WorldModel const& world, TeamPlan& plan, AreaPlan const& prev);
    };
}

#endif
