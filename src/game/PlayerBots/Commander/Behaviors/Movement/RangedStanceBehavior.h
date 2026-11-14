/*
 * PartyBot Commander layer 3: where a ranged damage dealer stands in combat ("rangedstance").
 * Design: .kiro/docs/2026-11-07/2026-11-07-pull-and-standing.md (5)
 *
 * Before: the original loop chased the victim to 25 yd (CombatBotBaseAI::BeginChasing) and
 * never looked at creatures that were not fighting yet. strat-20261107-152527: the mage stood
 * 20.3 yd from a wandering Ravaged Cadaver's home (aggro range 17, wander 5); after 14 s it
 * stepped close enough and its group of seven came.
 *
 * Now, while this module is on, it takes that chase over (Takeover::Stance: PartyBotAI::
 * BeginChasing stands down for bots that keep a caster distance, CombatBotBaseAI::
 * UsesCasterChase) and moves the bot itself. Bots that fight in melee (no mana, no ranged
 * weapon, a form) keep the original chase.
 *
 * When to move (checked every tick; since 2026-11-11 the current spot against the best cell,
 * doc .kiro/docs/2026-11-11-spot-scoring.md step 2):
 *  - the spot here is no longer valid: an idle creature would attack it, also once a wanderer
 *    walked to the edge of its circle (SafeRoute::IdleCreatures::Near; tier Urgent), or the
 *    victim is farther than CAST_RANGE (to its edge) or out of line of sight (tier Assigned,
 *    benefit = the bot's lost action over HORIZON_SEC: it cannot do its job). Then the best
 *    cell that passes the live checks.
 *  - otherwise: loss here = Exposure::LossAt(here) (Perception/Values/Exposure.h, the enemies
 *    that fight someone else turning to the bot; the walk here is 0). A cell is taken when its
 *    loss (SpotCost::Walk + Exposure::LossAt) is lower by more than SpotCost::Walk(
 *    MOVE_MARGIN_SEC): tier Assigned, benefit = the exposure saved, cost = the walk. So the bot
 *    stands back as far as the walk is worth it, with no distance lines (the 12 / 14 yd lines
 *    of 2026-11-10 are gone). Still within DANGER_ENEMY_GAP of such an enemy's edge: tier
 *    Urgent, score -walk seconds (the tier rule stays, doc section 5.6).
 *    No exposure before the fight.
 *  - The area phase's caster (TeamPlan::area PlacesCaster: the phase, or its prep spot;
 *    AreaSpot.h, 2026-11-12): the area phase places it. Before the fight nothing here; in the
 *    fight only an idle creature's reach (Urgent), not the victim out of range or sight.
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
 * Where (since 2026-11-10, position field step C): cells of the position field
 * (FieldSpots::Collect) up to MAX_RADIUS from the victim's edge (no inner limit since
 * 2026-11-11: near cells lose on exposure; MAX_RADIUS < CAST_RANGE leaves room for the victim
 * to move before the spot is out of range) (before the fight out to
 * PREPARE_MAX_RADIUS: a walk-in fought where the pack stands has the idle pack as the anchor,
 * the near cells lie in its aggro range). The field drops cells off the ground or cut off from
 * the party (a slope is fine; the rings before compared heights with the victim's), under an
 * idle creature, on a patrol's way before the fight ends, and cells the bot cannot walk to.
 * Kept: outside areas dangerous to ranged (HazardZones). Every cell's loss up front: walk +
 * exposure + sight margin (SightMargin::LossAt, Perception/Values/SightMargin.h: the share of
 * the pile's other places it does not see, by the field's estimate; 2026-11-14,
 * stockade-20261114-224312: the mage stood in the doorway and lost the victim whenever it moved
 * further in). Order (2026-11-15, the viewshed): cells the field estimates in sight of the
 * victim (PositionField::SeesEstimate, grid lines over the navmesh links) first, then the rest,
 * each by loss. One pass in that order: this bot's own idle scan, cells whose path failed a
 * moment ago skipped (SightCache::PathFailed), the real line of sight from the field's sight
 * cache (SightCache.h, kept per pair of cells, at most MAX_SIGHT_CHECKS new checks per tick),
 * then the spell's line now (SightCache::Live) and SafeRoute::PathIsSafe on at most
 * MAX_GEOMETRY_CHECKS cells; the first that passes is taken. In the fight the walk counts a
 * circle covering the victim's spot only closer than the victim (ArrivingAt, MeleeRoutes.h).
 * (Before: cells probed nearest first with the real line only, those behind a wall used the
 * budget and the path checks, stockade-20261115-002203.) Nothing found, or no field: the bot
 * stays (no chase into a pack).
 * The spot here costs its exposure + its sight margin (in reach only); the move's reason
 * "sees the victim only narrowly" when the margin saved more than the exposure.
 * Line of sight as the spell checks it (SightCache::Live: collision height to target + 1 yd;
 * before 2026-11-14 eye to eye at 2 yd).
 * Cost = SpotCost::Walk(bot, walk seconds), walk = the grid walk at the bot's run speed.
 * holdMs = walk seconds x 1000 + HOLD_EXTRA_MS.
 * Hazard zones stay an exclusion here (PositioningBehavior owns leaving them); they become a
 * SpotCost::Hazard term once that module moves to the same scoring.
 */

#ifndef MANGOS_COMMANDER_RANGED_STANCE_BEHAVIOR_H
#define MANGOS_COMMANDER_RANGED_STANCE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"
#include "Commander/Perception/Values/StandLoss.h"

namespace Commander
{
    class RangedStanceBehavior : public IBehavior
    {
    public:
        // Trigger: the victim farther than this from its edge (spells and wands reach 30).
        static constexpr float CAST_RANGE = 29.0f;
        // Farthest candidate cell from the victim's edge (28 before the fight).
        static constexpr float MAX_RADIUS = StandLoss::RANGED_MAX_YD;    // (the fight spot's too)
        static constexpr float PREPARE_MAX_RADIUS = 28.0f;
        // Nearer than this to the edge of an enemy fighting someone else: the move is Urgent
        // (same line as the healer stance; the tier only, not where to go).
        static constexpr float DANGER_ENEMY_GAP = 8.0f;
        // A valid spot is left only for a cell that saves more than this many seconds of the
        // bot's action (doc 2026-11-11-spot-scoring.md section 5.3; noise, the walk is in the loss).
        static constexpr float MOVE_MARGIN_SEC = 1.0f;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 6;    // pathfinding, cells that passed the sight check
        // Real line of sight checks not in the sight cache yet, per tick.
        static constexpr uint32 MAX_SIGHT_CHECKS = 48;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float HORIZON_SEC = 8.0f;
        static constexpr float PATH_FACTOR = StandLoss::RANGED_PATH_FACTOR;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "rangedstance"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
