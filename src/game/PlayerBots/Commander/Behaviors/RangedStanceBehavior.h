/*
 * PartyBot Commander layer 3: where a ranged damage dealer stands in combat ("rangedstance").
 * Design: .kiro/docs/2026-11-07/2026-11-07-pull-and-standing.md (5)
 *
 * Before: the original loop chased the victim to 25 yd (CombatBotBaseAI::BeginChasing) and
 * never looked at creatures that were not fighting yet. strat-20261107-152527: the mage stood
 * 20.3 yd from a wandering Ravaged Cadaver's home (aggro range 17, wander 5); after 14 s it
 * stepped close enough and its group of seven came.
 *
 * Now, while this module is on, it takes that chase over (Takeover::CasterChase: PartyBotAI::
 * BeginChasing stands down for bots that keep a caster distance, CombatBotBaseAI::
 * UsesCasterChase) and moves the bot itself. Bots that fight in melee (no mana, no ranged
 * weapon, a form) keep the original chase.
 *
 * When to move (checked every tick):
 *  - the bot stands where an idle creature would attack it, also once a wanderer walked to the
 *    edge of its circle (SafeRoute::IdleCreatures::Near): a constraint, tier Urgent;
 *  - the victim is farther than CAST_RANGE (to its edge) or out of line of sight: tier
 *    Assigned, benefit = the bot's lost action over HORIZON_SEC (it cannot do its job).
 * Before the fight (2026-11-07): out of combat while the pull runs, the brain's planned fight
 * spot (PullPlan::PlannedFightSpot; the idle pack itself for a walk-in fought where it stands)
 * instead of a victim, same checks, rings PREPARE_RADII (one more at 28 yd); value
 * Value::ContributionPerSecAt(PullPlan::expectedIncomingDps) x walk seconds, cost 0 (see
 * HealerStanceBehavior.h). No planned spot: it waits where it is (PullPlan::HoldsWaiting).
 * Runs while the party fights (WorldModel::InFight), also for a bot not in combat itself; no
 * victim yet: the brain's target for it (TeamPlan::targets; TargetBehavior attacks only in
 * range and sight). Ranged bots do not follow the leader then (PartyBotAI::IsStanceControlled).
 * Not while: the retreat runs, the pull is in Approach / Return, out of combat and held by
 * the pull (PullPlan::HoldsWaiting), rooted, casting, the victim chases the bot itself (the
 * escape module's job), or a MoveTo is under way (then Hold so the rotation's first cast does
 * not stop the walk).
 *
 * Where: rings RADII around the victim (plus its combat reach), RING_POINTS directions
 * starting from the bot's side, nearest walk first. A point is kept when it is outside every
 * idle creature's reach, outside areas dangerous to ranged (HazardZones), not where an idle
 * patrol walks before the fight should end (PatrolAssigner::InPath), its ground within
 * MAX_STEP_Z of the victim's, in line of sight of the victim, and the path there is safe
 * (SafeRoute::PathIsSafe). At most MAX_GEOMETRY_CHECKS points per tick.
 * Nothing found: the bot stays (no chase into a pack).
 * Cost = Value::LostAction(bot, walk seconds). holdMs = walk seconds x 1000 + HOLD_EXTRA_MS.
 */

#ifndef MANGOS_COMMANDER_RANGED_STANCE_BEHAVIOR_H
#define MANGOS_COMMANDER_RANGED_STANCE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class RangedStanceBehavior : public IBehavior
    {
    public:
        // Trigger: the victim farther than this from its edge (spells and wands reach 30).
        static constexpr float CAST_RANGE = 29.0f;
        static constexpr uint32 RING_POINTS = 12;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 6;    // GetHeight + LOS + pathfinding each
        static constexpr float MAX_STEP_Z = 4.0f;
        static constexpr float LOS_EYE_HEIGHT = 2.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float HORIZON_SEC = 8.0f;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "rangedstance"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
