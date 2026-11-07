/*
 * PartyBot Commander layer 3: where a healer stands in combat ("stance" module).
 *
 * Before: a healer had no target, so in combat it followed the party leader at 3-6 yd
 * (PartyBotAI::UpdateAI). In the Stockade test (combat recording stockade-20261101-143825) the
 * player walked into the next cell, the priest followed into that pack, then stood 1-4 yd from
 * the enemies for the rest of the fight: 8 heals cancelled (at least 3 by Kick), 23 casts in
 * 183 s, tank dead at 65 s.
 *
 * Now, while this module is on, PartyBotAI does not follow the leader with a healer in combat
 * (PartyBotAI::HasCombatStance); this module moves it instead.
 *
 * Anchor (who to stay with): a tank (Perception/Tanks.h, bot or player; with several, the one
 * most enemies hit); else the member most enemies are hitting; else the party leader. Other
 * tanks within heal range of a spot count TANK_COVER_MULT members in its order.
 *
 * When to move (checked every tick, nothing otherwise):
 *  - the healer stands where an idle creature would attack it, also once a wanderer walked to
 *    the edge of its circle (SafeRoute::IdleCreatures::Near; added 2026-11-07,
 *    strat-20261107-164236: the priest stood 17.8 yd from a wandering Ravaged Cadaver's home,
 *    reach 17 + 5 margin + wander 5, its group of seven came 4 s later and killed her). A
 *    constraint as in the "rangedstance" module: tier Urgent, score -walk seconds, so it wins
 *    over the routine heal orders (Assigned) and an emergency heal order (Urgent, score > 0)
 *    still goes first. The walk out may pass inside the circles that cover the start, no
 *    closer to their homes (IdleCreatures::LeavingFrom); the new spot must be outside all;
 *  - the anchor is farther than HEAL_RANGE or out of line of sight ("tank out of reach"), or
 *  - an enemy that is not hitting the healer stands within TRIGGER_ENEMY_GAP ("too close").
 *    Enemies hitting the healer are left to the rotation (Fade) and the escape module (run
 *    to the tank: EscapeBehavior.h).
 * Not while: the retreat runs, the pull is in Approach / Return, rooted, casting (the heal
 * finishes first), a member below URGENT_HEALTH_PCT is healable from here (heal first; not
 * when standing in an idle creature's reach, see above), or a MoveTo is already under way
 * (then Hold so the rotation's first cast does not stop the walk; tier Urgent while still in
 * an idle creature's reach).
 *
 * Where: rings around the anchor (STRICT_RADII), starting straight behind it as seen from the
 * enemies near it, then turning both ways. A point is kept when:
 *  - every enemy not hitting the healer is at least MIN_ENEMY_GAP away (second pass:
 *    RELAXED_ENEMY_GAP on RELAXED_RADII);
 *  - outside every area dangerous to healers (HazardZones, same as the "position" module);
 *  - neither the point nor the path there is within an idle creature's aggro range (SafeRoute);
 *  - no idle patrol's predicted walk comes within its aggro range + PatrolAssigner::ZONE_MARGIN
 *    of the point before the fight should end (PatrolAssigner::InPath on WorldModel::patrols;
 *    added 2026-11-03 for the line of sight pull, where the tank hides around a corner and the
 *    healer follows it out of sight on its own instead of the pull asking healers to see it);
 *  - ground within MAX_STEP_Z of the anchor, line of sight to the anchor.
 * Order: walk distance (-1 per yd), other members within heal range of the point
 * (+COVER_WEIGHT each), enemy distance (+GAP_WEIGHT per yd up to GAP_CAP). The map checks
 * (height, line of sight, pathfinding) run for at most MAX_GEOMETRY_CHECKS points per tick.
 * Nothing found and the anchor out of reach, or the healer in an idle creature's reach: walk
 * to the anchor itself if it stands outside every idle reach and that path is safe.
 *
 * Value (points): benefit - cost, horizon H = min(HORIZON_SEC, fight remaining), at least 1 s.
 *  benefit = Value::LostAction(healer, H) when the anchor is out of reach (the healer cannot do
 *            its job), plus for every enemy closer than MIN_ENEMY_GAP that does not hit the
 *            healer: TURN_CHANCE x (HitLoss(enemy, healer, gain) + LostAction(healer, gain)),
 *            gain = extra distance / ENEMY_RUN_SPEED (at most MAX_GAIN_SEC): the seconds an enemy
 *            that turns to the healer needs to reach it, i.e. hits and cast pushback it does not
 *            get before the tank takes it back.
 *  cost    = Value::LostAction(healer, walk seconds), x IDLE_COST_FACTOR when no member is
 *            below INJURED_PCT (walking costs little while nothing needs a heal).
 * holdMs = walk seconds x 1000 + HOLD_EXTRA_MS. After that the healer stays where it is.
 *
 * Jitter: moves only past the trigger lines, the targets are inside them (gap 12 -> 14, second
 * pass 12; reach 28 -> in range with line of sight). Gaps are measured to the enemy's edge
 * (EnemyFacts::boundingRadius).
 *
 * Known limits: anchor and enemy positions are from the last refresh; other heal targets only
 * add to the order (a member out of range or sight is not a reason to move yet); one healer
 * does not avoid the spot of another; enemy run speed is a constant.
 */

#ifndef MANGOS_COMMANDER_HEALER_STANCE_BEHAVIOR_H
#define MANGOS_COMMANDER_HEALER_STANCE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class HealerStanceBehavior : public IBehavior
    {
    public:
        static constexpr float HEAL_RANGE = 28.0f;          // same as PositioningBehavior::HEAL_RANGE
        // Closer than this to an enemy's edge: move. Until step 24 (2026-11-01) 8 yd: the priest
        // stood 8.9 yd from Magistrate Barthilas, inside Crowd Pummel (10 yd, interrupt = 5 s
        // holy lockout; strat-20261101-214026). Most melee-range area spells are 8-10 yd.
        static constexpr float TRIGGER_ENEMY_GAP = 12.0f;
        static constexpr float MIN_ENEMY_GAP = 14.0f;       // new spot, first pass
        static constexpr float RELAXED_ENEMY_GAP = TRIGGER_ENEMY_GAP;  // new spot, second pass (not inside the trigger line)
        static constexpr uint32 RING_POINTS = 12;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 5;    // GetHeight + LOS + pathfinding each
        static constexpr float MAX_STEP_Z = 4.0f;
        static constexpr float LOS_EYE_HEIGHT = 2.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float ENEMY_SCAN = 30.0f;          // enemies this close to the anchor set "behind"

        static constexpr float COVER_WEIGHT = 4.0f;
        static constexpr float TANK_COVER_MULT = 3.0f;      // another tank in range counts this many members
        static constexpr float GAP_WEIGHT = 0.5f;
        static constexpr float GAP_CAP = 20.0f;

        static constexpr float HORIZON_SEC = 8.0f;
        static constexpr float TURN_CHANCE = 0.5f;          // an enemy turns to the healer within H
        static constexpr float ENEMY_RUN_SPEED = 7.0f;
        static constexpr float MAX_GAIN_SEC = 3.0f;
        static constexpr float URGENT_HEALTH_PCT = 35.0f;
        static constexpr float INJURED_PCT = 80.0f;
        static constexpr float IDLE_COST_FACTOR = 0.25f;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "stance"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        // Takeover::CasterChase in combat (2026-11-07): a healer with a victim (a damage spell
        // between heals) no longer chases it to 25 yd; this module keeps it with the anchor.
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
