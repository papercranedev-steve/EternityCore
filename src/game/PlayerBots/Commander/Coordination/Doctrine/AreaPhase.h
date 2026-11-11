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
 * Tanks sent elsewhere (patrolMove Move: walking off a patrol's path; read as PatrolAssigner
 * left it this refresh) take nothing back: AggroTurnCost::Options::busyTanks.
 *
 * 1. A bot channelling an area spell (PartyMemberFacts::areaChannel): the phase is its channel
 *    (step Channel), valued by AreaValue on the ticks left (paid). endCost = its net now -
 *    its net with every tank busy (what sending the tank away costs, PatrolAssigner pays it).
 * 2. Otherwise every damage dealer bot, every ready area spell (AreaSpellFacts), every centre
 *    (ground spell: each acting enemy within range; else around the caster), valued by
 *    AreaValue for the whole spell (never "start and stop after a few ticks"):
 *      here   cast from where it stands;
 *      spot   ground channels only: the point on the line from the centre through the caster
 *             at range - SPOT_MARGIN_YD, when that is SPOT_MIN_GAIN_YD farther (user
 *             2026-11-08: Blizzard from as far as it reaches, the slowed enemies take longer
 *             to come); its net - LostAction(walk at RUN_YD_PER_SEC). The cerebellum checks
 *             the ground there and casts from where it stands when it is not usable (netHere);
 *      shield its own shield first (SelfShield, Ice Barrier) when net with the absorb - net
 *             without > LostAction(GCD) + ManaWorth(its mana).
 *    The best positive net wins; the one of the last refresh gets STICKY_POINTS.
 * 3. Or it waits (2026-11-12, doc .kiro/docs/2026-11-11-aoe-threat.md 4.3 / 4.4; AreaPlan::wait,
 *    the phase is not active): when starting later is worth more than now, a wait of `sec`
 *    costing net(later) x sec / the spell's length:
 *      Tank    every enemy it would turn is on the main tank: later nobody turns (net + turn
 *              loss), sec = their ThreatRunway shortfalls through the spell / ThreatGap::
 *              FillRate (the tank's top-up goes for them: TankTargets reads the same runway).
 *              Not when their total did not shrink for ThreatOpeningAssigner::STALL_MS.
 *      Shield  its shield is on cooldown: later with it (as the shield step), sec = the cooldown.
 * While the pull or the opening holds the party (the "not while" above, not a retreat): under
 * the Area tactic the caster it counts on (DoctrinePlan::areaThreat) puts its shield up now
 * (AreaPlan::prep*) when the spell on the pile around the main tank, valued as the threat
 * stands, gains more with it than its mana (no GCD: it waits anyway; the opening's hold lets
 * that cast through, TeamPlan::HoldsCast).
 * Both: a healer bot's Power Word: Shield (SurvivalAbility Absorb, not self only, ready, in
 *   range, the caster without Weakened Soul) when net with it - net without > LostAction(healer,
 *   GCD) + ManaWorth(healer, mana) (user 2026-11-08: the party protects the caster).
 *   tauntReserve = net - net without the tanks' taunts (RescueAssigner charges it to a taunt
 *   spent on an enemy outside the area).
 * One area phase per party at a time.
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_AREA_PHASE_H
#define MANGOS_COMMANDER_AREA_PHASE_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class AreaPhaseAssigner : public IAssigner
    {
    public:
        static constexpr float SPOT_MARGIN_YD = 3.0f;
        static constexpr float SPOT_MIN_GAIN_YD = 4.0f;
        static constexpr float RUN_YD_PER_SEC = 7.0f;
        static constexpr float STICKY_POINTS = 1.0f;

        // Same id as AreaDamageBehavior: `.partybot ai module aoe off` switches both.
        char const* GetId() const override { return "aoe"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
