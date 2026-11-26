/*
 * PartyBot Commander layer 3: where a ranged damage dealer stands in combat ("rangedstance").
 * Design: .kiro/docs/2026-11-07/2026-11-07-pull-and-standing.md (5)
 *
 * Before: the original loop chased the victim to 25 yd (CombatBotBaseAI::BeginChasing) and
 * never looked at creatures that were not fighting yet. strat-20261107-152527: the mage stood
 * 20.3 yd from a wandering Ravaged Cadaver's home (aggro range 17, wander 5); after 14 s it
 * stepped close enough and its group of seven came.
 *
 * Now, while this module is on, it takes that chase over (Takeover::Stance: TeamBotAI::
 * BeginChasing stands down for bots that keep a caster distance, CombatBotBaseAI::
 * UsesCasterChase) and moves the bot itself. Bots that fight in melee (no mana, no ranged
 * weapon, a form) keep the original chase.
 *
 * When to move (checked every tick; since 2026-11-11 the current spot against the best cell,
 * doc .kiro/docs/2026-11-11-spot-scoring.md step 2):
 *  - the spot here is no longer valid: an idle creature would attack it, also once a wanderer
 *    walked to the edge of its circle (SafeRoute::IdleCreatures::Near; tier Urgent), or the
 *    victim is farther than CAST_RANGE (to its edge) or out of line of sight, or nearer than
 *    the bot's dead zone (ClassKit::MinAttackRange, hunter Auto Shot 8 yd; 2026-11-15: cells
 *    start DEAD_ZONE_MARGIN beyond it) (tier Assigned, benefit = SpotCost::OutOfReach, the
 *    bot's action for the fight left: it cannot do its job; before 2026-11-15 an 8 s horizon,
 *    a walk longer than that never went, library-20261115-220153). Then the best cell that
 *    passes the live checks.
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
 *    Backup (2026-11-26): sent to a spot it is not at (Move step or prep) and out of the
 *    victim's reach: the usual walk into reach at Routine tier, so any proposal of the area
 *    phase hides it; it moves the caster only when the area phase's walk does not come.
 * Before the fight (2026-11-07): out of combat while the pull runs, the brain's planned fight
 * spot (PullPlan::PlannedFightSpot; the idle pack itself for a walk-in fought where it stands)
 * instead of a victim, same checks, rings PREPARE_RADII (one more at 28 yd); value
 * Value::ContributionPerSecAt(PullPlan::expectedIncomingDps) x walk seconds, cost 0 (see
 * HealerStanceBehavior.h). No planned spot: it waits where it is (PullPlan::HoldsWaiting).
 * A bot eating or drinking stays seated (only an idle creature's reach moves it): it walks
 * to its place once RestBehavior gets it up.
 * Runs while the party fights (WorldModel::InFight), also for a bot not in combat itself; no
 * victim yet: the brain's target for it (TeamPlan::targets; TargetBehavior attacks only in
 * range and sight). Ranged bots do not follow the leader then (TeamBotAI::IsStanceControlled).
 * Not while: the retreat runs, the pull is in Approach / Return, out of combat and held by
 * the pull (PullPlan::HoldsWaiting), rooted, casting, the victim chases the bot itself (the
 * escape module's job), or a MoveTo is under way (then Hold so the rotation's first cast does
 * not stop the walk).
 *
 * Where (since 2026-11-10, position field step C; the search itself is RangedSpot::Find since
 * 2026-11-15, shared with KiteBehavior): cells of the position field
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
 * moment ago skipped (SightCache::PathFailed), cells it stood on a moment ago in range but out
 * of the victim's sight skipped (SightCache::StandFailed, marked here; 2026-11-25,
 * stockade-20261125-221620: the hunters on the ramp below the platform walked between two
 * cells for 8 s, each passed the check from the other and failed once stood on), the real line of sight from the field's sight
 * cache (SightCache.h, kept per pair of cells, at most MAX_SIGHT_CHECKS new checks per tick),
 * then the spell's line now from the cell's ground (SightCache::LiveFromCell) and SafeRoute::PathIsSafe on at most
 * RangedSpot::MAX_GEOMETRY_CHECKS cells; the first that passes is taken. In the fight the walk counts a
 * circle covering the victim's spot only closer than the victim (ArrivingAt, MeleeRoutes.h).
 * (Before: cells probed nearest first with the real line only, those behind a wall used the
 * budget and the path checks, stockade-20261115-002203.) In the fight the cells the grid walk
 * does not reach come after all of those, same order (FieldSpots::Query::keepUnreached,
 * 2026-11-15, stockade-20261115-143912: the grid walk reached only the next rooms, none saw the
 * tank's cell, the mages stood 70 s): SafeRoute::PathIsSafe decides, and a path blocked by an
 * idle creature gives the join gamble its blocker. Nothing found, or no field: the bot
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
        // Real line of sight checks not in the sight cache yet, per tick (path checks, detour
        // and walk factor: RangedSpot.h).
        static constexpr uint32 MAX_SIGHT_CHECKS = 48;
        // Cells start this far beyond the bot's dead zone (ClassKit::MinAttackRange): the victim
        // shifts a little in the fight, a spot right on the line would be left again.
        static constexpr float DEAD_ZONE_MARGIN = 3.0f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "rangedstance"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        bool TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
