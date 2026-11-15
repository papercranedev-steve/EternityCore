/*
 * PartyBot Commander layer 3: where a healer stands in combat ("stance" module).
 *
 * Before: a healer had no target, so in combat it followed the party leader at 3-6 yd
 * (PartyBotAI::UpdateAI). In the Stockade test (combat recording stockade-20261101-143825) the
 * player walked into the next cell, the priest followed into that pack, then stood 1-4 yd from
 * the enemies for the rest of the fight: 8 heals cancelled (at least 3 by Kick), 23 casts in
 * 183 s, tank dead at 65 s.
 *
 * Now PartyBotAI does not follow the leader with a healer while the party fights
 * (PartyBotAI::IsStanceControlled); this module moves it instead. "The party fights"
 * (WorldModel::InFight), not "the healer is in combat": before 2026-11-07 a healer that had
 * not healed yet kept following the player while the tank fought further away.
 *
 * Before the fight (added 2026-11-07, user: everyone takes their place while the pull is
 * planned, not once in combat): out of combat while the pull runs and the brain knows where the
 * tank will fight (PullPlan::PlannedFightSpot: the spot when bringing the pack back, the shot or
 * face-pull spot when fought in place, the pack itself for a walk-in fought where it stands,
 * the arrival / line of sight point in Settle), that spot is the anchor and "behind" is away
 * from PullPlan::packAt (the anchor is the pack: towards the healer's side). Same checks and
 * rings plus a last pass on PREPARE_OUTER_RADII at the edge of heal range (the idle pack's
 * aggro range covers the inner rings; no spot outside it: the healer waits). Value: the
 * walk costs nothing now and saves walking it during the fight: Value::ContributionPerSecAt
 * (PullPlan::expectedIncomingDps, the FightLoad estimate) x walk seconds, which has to beat
 * PullBehavior's wait (Hold). No planned spot (the opening hold past the arrival): it waits
 * where it is (PullPlan::HoldsWaiting). Not in WaitReady (drinking).
 *
 * Anchor (who to stay with): a tank (Perception/Tanks.h, bot or player; with several, the one
 * most enemies hit); else the member most enemies are hitting; else the party leader. Other
 * tanks within heal range of a spot count TANK_COVER_MULT members in its order.
 *
 * When to move (checked every tick; since 2026-11-11 the current spot against the best cell,
 * doc .kiro/docs/2026-11-11-spot-scoring.md step 3). The spot here is no longer valid, so the
 * best cell is taken whatever it saves, when:
 *  - the healer stands where an idle creature would attack it, also once a wanderer walked to
 *    the edge of its circle (SafeRoute::IdleCreatures::Near; added 2026-11-07,
 *    strat-20261107-164236: the priest stood 17.8 yd from a wandering Ravaged Cadaver's home,
 *    reach 17 + 5 margin + wander 5, its group of seven came 4 s later and killed her). A
 *    constraint as in the "rangedstance" module: tier Urgent, score -walk seconds, so it wins
 *    over the routine heal orders (Assigned) and an emergency heal order (Urgent, score > 0)
 *    still goes first. The walk out may pass inside the circles that cover the start, no
 *    closer to their homes (IdleCreatures::LeavingFrom); the new spot must be outside all;
 *  - the anchor is farther than HEAL_RANGE or out of line of sight ("tank out of reach").
 * Otherwise the spot here costs StayLoss (the "reach" and "enemies" terms of the order below,
 * the walk is 0) and a cell is taken when its loss is lower by more than SpotCost::Walk(
 * MOVE_MARGIN_SEC) (the margin is not scaled by IDLE_COST_FACTOR: it is noise, not a walk).
 * So the healer stands back from the enemies and towards members out of reach as far as the
 * walk is worth it, with no distance lines (the 12 / 14 yd lines, the relaxed pass and the
 * "3 yd farther" fallback pass of 2026-11-10 are gone). Before the fight a valid spot is kept
 * (the others are still walking to their places).
 * Enemies hitting the healer do not count (Exposure::Turnable): they are left to the rotation
 * (Fade) and the escape module (run to the tank: EscapeBehavior.h).
 * Not while: the retreat runs, the pull is in Approach / Return, rooted, casting (the heal
 * finishes first), a member below URGENT_HEALTH_PCT is healable from here (heal first; not
 * when standing in an idle creature's reach, see above), or a MoveTo is already under way
 * (then Hold so the rotation's first cast does not stop the walk; tier Urgent while still in
 * an idle creature's reach).
 *
 * Where (since 2026-11-10, position field step C): cells of the position field
 * (FieldSpots::Collect) within MAX_YD of the anchor (PREPARE_MAX_YD before the fight); no inner
 * limit and no bands since 2026-11-11 (near cells lose on the enemies' term; MAX_YD below
 * HEAL_RANGE leaves room for the anchor to move). The field drops cells off the ground or cut off from the
 * party (a slope is fine; the rings before compared heights with the anchor's), under an idle
 * creature, where an idle patrol's predicted walk comes within its aggro range + margin before
 * the fight should end (added 2026-11-03 for the line of sight pull), and cells the healer
 * cannot walk to around idle creatures. A cell is kept when it is outside every area dangerous
 * to healers (HazardZones; an exclusion until PositioningBehavior uses the same scoring).
 * Order (points, since 2026-11-10 step E; lowest loss first, ties shared out by
 * FieldSpots::PreferFree within one cell of walk):
 *   walk     IDLE_COST_FACTOR (nobody injured) or 1 x LostAction(healer, grid walk / run speed)
 *   reach    for every other member (not the anchor) farther than HEAL_RANGE from the spot:
 *            its share of the party's damage taken (Value::TakenDps; equal shares while nobody
 *            takes any) x SpotCost::TargetReach (LostAction of the walk to reach it =
 *            (distance - HEAL_RANGE) x PATH_FACTOR / run speed, at most H). Another tank takes
 *            most damage and counts the most.
 *   enemies  Exposure::LossAt (Perception/Values/Exposure.h, shared with the ranged stance):
 *            for every enemy not hitting the healer TURN_CHANCE x (HitLoss + LostAction) over
 *            the seconds an enemy that turns to the healer gets before the tank takes it back.
 * Before: yards with made-up weights (-1 per yd walk, +4 per member in range, tanks x3,
 * +0.5 per yd of enemy gap up to 20).
 *   sight    (in the fight, cells that see the anchor; 2026-11-14, stockade-20261114-224312:
 *            the priest stood in the doorway of the tank's cell) SightMargin::LossAt
 *            (Perception/Values/SightMargin.h): the share of the enemies around the tank the
 *            spot does not see. Also part of StayLoss here when the anchor is in reach.
 * Order and checks as the ranged stance (2026-11-15, the viewshed): cells the field estimates
 * in sight of the anchor (PositionField::SeesEstimate) first, then the rest, each by loss; one
 * pass: this healer's own idle scan, recent path failures skipped, the real line from the
 * field's sight cache (SightCache.h, at most MAX_SIGHT_CHECKS new checks per tick), then the
 * heal's line now (SightCache::Live) and SafeRoute::PathIsSafe (in the fight ArrivingAt the
 * anchor) on at most MAX_GEOMETRY_CHECKS cells. Line of
 * sight as the spell checks it (collision height to target + 1 yd; before 2026-11-14 eye to
 * eye at 2 yd). (The rings started straight behind the anchor; that only broke ties of the
 * order and is gone.)
 * Nothing found and the anchor out of reach, or the healer in an idle creature's reach: walk
 * to the anchor itself if it stands outside every idle reach and that path is safe.
 *
 * Value (points): benefit - cost.
 *  benefit = SpotCost::OutOfReach(healer) when the anchor is out of reach (the healer cannot do
 *            its job for the fight left; before 2026-11-15 an 8 s horizon, a longer walk never
 *            went), plus StayLoss(here) - StayLoss(there) (the order's terms).
 * Within DANGER_ENEMY_GAP of an enemy's edge (2026-11-10, the priest stood in the pile next to
 * the tank and died when one turned): tier Urgent, score -walk seconds, like the idle reach
 * (the tier only; where to go is the same order).
 *  cost    = SpotCost::Walk(healer, walk seconds), x IDLE_COST_FACTOR when no member is
 *            below INJURED_PCT (walking costs little while nothing needs a heal).
 * holdMs = walk seconds x 1000 + HOLD_EXTRA_MS. After that the healer stays where it is.
 *
 * Jitter: the margin above, and cells within MAX_YD (24) while the anchor counts as in reach up
 * to HEAL_RANGE (28). Gaps are measured to the enemy's edge (EnemyFacts::boundingRadius).
 *
 * Known limits: anchor and enemy positions are from the last refresh; other heal targets only
 * add to the order (a member out of range or sight is not a reason to move yet); one healer
 * does not avoid the spot of another; enemy run speed is a constant.
 */

#ifndef MANGOS_COMMANDER_HEALER_STANCE_BEHAVIOR_H
#define MANGOS_COMMANDER_HEALER_STANCE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"
#include "Commander/Perception/Values/StandLoss.h"

namespace Commander
{
    class HealerStanceBehavior : public IBehavior
    {
    public:
        // (the cell terms: Perception/Values/StandLoss.h)
        static constexpr float HEAL_RANGE = StandLoss::HEAL_RANGE;      // same as PositioningBehavior::HEAL_RANGE
        // Farthest candidate cell from the anchor (before the fight: just inside HEAL_RANGE).
        static constexpr float MAX_YD = StandLoss::HEALER_MAX_YD;
        static constexpr float PREPARE_MAX_YD = 27.0f;
        // A valid spot is left only for a cell that saves more than this many seconds of the
        // healer's action (doc 2026-11-11-spot-scoring.md section 5.3).
        static constexpr float MOVE_MARGIN_SEC = 1.0f;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 5;    // pathfinding, cells that passed the sight check
        // Real line of sight checks not in the sight cache yet, per tick.
        static constexpr uint32 MAX_SIGHT_CHECKS = 48;
        static constexpr float MAX_PATH_DETOUR = 1.8f;

        // Closer than this to an enemy's edge (in its melee, cleave and most area spells):
        // the move is Urgent (2026-11-10; the tier only, not where to go).
        static constexpr float DANGER_ENEMY_GAP = 8.0f;

        static constexpr float URGENT_HEALTH_PCT = 35.0f;
        static constexpr float INJURED_PCT = StandLoss::INJURED_PCT;
        static constexpr float IDLE_COST_FACTOR = StandLoss::IDLE_COST_FACTOR;
        static constexpr float PATH_FACTOR = StandLoss::HEALER_PATH_FACTOR;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "stance"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        // Takeover::Stance in combat (2026-11-07): a healer with a victim (a damage spell
        // between heals) no longer chases it to 25 yd; this module keeps it with the anchor.
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
