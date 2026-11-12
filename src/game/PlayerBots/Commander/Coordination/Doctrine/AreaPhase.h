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
 *      here   cast from where it stands (in range);
 *      spot   ground channels: the best place of the position field (AreaSpot.h, 2026-11-12;
 *             until then one point on the line centre -> caster at range - 3 yd); its net -
 *             the walk. The cerebellum checks sight and path to AreaPlan::spots in order and
 *             casts from where it stands when none is usable (netHere);
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
 *              FillRate (the tank's top-up goes for them: TankTargets reads the same runway).
 *              Not when their total did not shrink for ThreatOpeningAssigner::STALL_MS.
 *      Shield  its shield is on cooldown: later with it (as the shield step), sec = the cooldown.
 *    Meanwhile its spot is the prep spot below (it walks there while it waits).
 * While the pull or the opening holds the party (the "not while" above, not a retreat), the
 * caster gets ready (AreaPlan::prep*; doc 4.4, 4.5, 8, user 2026-11-12: stand there first, a
 * turn predicted is too late):
 *   who     under the Area tactic the caster it counts on (DoctrinePlan::areaThreat); before
 *           the fight (no tactic yet) the area dealers when the pull expects at least
 *           TacticalDoctrine::AREA_MIN enemies; the most threat per second (AreaIntent);
 *   centre  the enemy on the main tank nearest it, else the planned fight spot (PullPlan::
 *           PlannedFightSpot);
 *   spot    AreaSpot for that centre (AreaValue only with enemies there);
 *   shield  whenever it is ready and not up (doc mage-hold 3.6), worth the health it takes
 *           (the opening's hold lets that cast through, Constraints::OpeningForbids).
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
        static constexpr float STICKY_POINTS = 1.0f;

        // Same id as AreaDamageBehavior: `.partybot ai module aoe off` switches both.
        char const* GetId() const override { return "aoe"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
