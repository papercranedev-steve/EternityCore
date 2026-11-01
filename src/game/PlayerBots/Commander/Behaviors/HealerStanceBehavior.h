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
 * Anchor (who to stay with): the tank bot; else a member in a tank stance; else the member most
 * enemies are hitting; else the party leader.
 *
 * When to move (checked every tick, nothing otherwise):
 *  - the anchor is farther than HEAL_RANGE or out of line of sight ("tank out of reach"), or
 *  - an enemy that is not hitting the healer stands within TRIGGER_ENEMY_GAP ("too close").
 *    Enemies hitting the healer are left to the rotation (Fade, run to the tank: FleePoint).
 * Not while: the retreat runs, the pull is in Approach / Return, rooted, casting (the heal
 * finishes first), a member below URGENT_HEALTH_PCT is healable from here (heal first), or a
 * MoveTo is already under way (then Hold so the rotation's first cast does not stop the walk).
 *
 * Where: rings around the anchor (STRICT_RADII), starting straight behind it as seen from the
 * enemies near it, then turning both ways. A point is kept when:
 *  - every enemy not hitting the healer is at least MIN_ENEMY_GAP away (second pass:
 *    RELAXED_ENEMY_GAP on RELAXED_RADII);
 *  - outside every area dangerous to healers (HazardZones, same as the "position" module);
 *  - neither the point nor the path there is within an idle creature's aggro range (SafeRoute);
 *  - ground within MAX_STEP_Z of the anchor, line of sight to the anchor.
 * Order: walk distance (-1 per yd), other members within heal range of the point
 * (+COVER_WEIGHT each), enemy distance (+GAP_WEIGHT per yd up to GAP_CAP). The map checks
 * (height, line of sight, pathfinding) run for at most MAX_GEOMETRY_CHECKS points per tick.
 * Nothing found and the anchor out of reach: walk to the anchor itself if that path is safe.
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
 * Jitter: moves only past the trigger lines, the targets are well inside them (gap 8 -> 14,
 * reach 28 -> in range with line of sight).
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
        static constexpr float TRIGGER_ENEMY_GAP = 8.0f;    // closer than this: move
        static constexpr float MIN_ENEMY_GAP = 14.0f;       // new spot, first pass
        static constexpr float RELAXED_ENEMY_GAP = 8.0f;    // new spot, second pass
        static constexpr uint32 RING_POINTS = 12;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 5;    // GetHeight + LOS + pathfinding each
        static constexpr float MAX_STEP_Z = 4.0f;
        static constexpr float LOS_EYE_HEIGHT = 2.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float ENEMY_SCAN = 30.0f;          // enemies this close to the anchor set "behind"

        static constexpr float COVER_WEIGHT = 4.0f;
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
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
