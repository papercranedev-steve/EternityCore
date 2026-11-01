/*
 * PartyBot Commander layer 3: step out of enemy areas ("position" module).
 * Design: .kiro/docs/2026-11-01-positioning.md. Zones: Perception/HazardZones.h.
 *
 * Who: healers and ranged damage (control and damage areas), melee damage (only Pulse areas
 * and Burst spells whose damage is a periodic aura, e.g. Melt Ore). Not tanks.
 *
 * When: the bot stands at least ENTER_MARGIN inside a zone dangerous to its role. Nothing while
 * the retreat runs (RetreatBehavior owns movement at Override tier), during the pull's
 * Approach / Return phases, out of combat, rooted, or while already walking under a movement
 * hold (a MoveTo of this or another module in progress).
 *
 * Where: RING_POINTS points on a circle around the zone centre, radius = zone radius (spell
 * radius + 1) + RING_EXTRA (melee also + MELEE_RING_EXTRA, to stay in reach of a large target).
 * A point is kept when:
 *  - it is outside every zone dangerous to this role (second pass: Range zones may stay);
 *  - no creature that is not fighting yet would attack the bot there, nor along the straight
 *    walk there (SafeRoute::IdleCreatures scanned around the bot: aggro range + margin, LOS);
 *  - healer: the holder (tank bot, or whoever most enemies are hitting) is within HEAL_RANGE
 *    and in line of sight; ranged: the current victim is within RANGED_RANGE and in sight;
 *  - ground found within MAX_STEP_Z of the bot (Map::GetHeight), and line of sight from the
 *    bot to the point (stand-in for "reachable"; MovePoint then path-finds).
 * Nearest first (melee: points that keep melee reach first). The map queries run for at most
 * MAX_GEOMETRY_CHECKS points per tick.
 *
 * Value (points, value-scoring doc): benefit - cost.
 *  hits(zone) over H = min(max(HORIZON_SEC, repeat delay), MAX_HORIZON_SEC, fight remaining):
 *    Pulse        H / period
 *    Burst/Range  H / repeat delay; while the enemy casts it: 1 + (H - remaining) / repeat
 *                 when the bot gets out before the cast ends (walk + DODGE_SLACK_SEC <= remaining),
 *                 else the current cast lands anyway and only later casts are saved.
 *  benefit = sum over zones left of hits x hitChance x HazardZones::HitPoints
 *            - the same for zones newly entered (second pass only)
 *            + death penalty when the expected damage of staying reaches the bot's health
 *              and moving brings it below (then tier Urgent, else Assigned).
 *  cost    = Value::LostAction(bot, walk seconds): a cast-time spell stops the walk, so walking
 *            is counted as lost action time (instants while moving are not credited).
 *            Melee leaving reach of its victim: LostAction(walk + time away), time away = the
 *            remaining cast for a dodge, else H.
 * holdMs = walk / run speed x PATH_FACTOR + HOLD_EXTRA_MS; a dodge holds until the cast ends;
 * melee out of reach holds for the time away, at most MELEE_AWAY_MAX_SEC unless dodging.
 *
 * Melee out of reach (not a dodge): one step per area. The step is recorded on the bot
 * (PartyBotAI::m_hazardGiveUp: zone enemy + spell); after its hold the chase takes the bot
 * back in, and while it stays inside that area no more out-of-reach steps are proposed
 * (dodges, steps that keep reach and Urgent / lethal steps still are). Re-armed when the bot
 * leaves the area again, when the area is gone, or when the bot is not back within
 * MELEE_RETURN_MS after the hold. Before: a hold of up to MAX_HORIZON_SEC, repeated after
 * every chase back in, kept the melee out of the fight.
 *
 * Jitter: proposes only ENTER_MARGIN inside; the target ring is RING_EXTRA outside, so a bot
 * needs to drift back ~3.5 yd before the next move. Apart from the melee record above there is
 * no "just moved" timer; the margins are the hysteresis. In combat PartyBotAI does not start
 * following the leader while the leader stands in an area dangerous to the bot
 * (PartyBotAI::IsInHazardZone), so a healer is not walked straight back in after the hold.
 *
 * Known limits: stand-in reachability (line of sight, not a path: corridors, stairs and
 * ledges can still fail, MovePoint then walks around, and the idle creature check covers the
 * straight walk, not that detour); zones are
 * from the last refresh (moving casters); the chase (ranged at 25 yd, melee) may still walk
 * the bot back in after the hold, the next tick then moves it out again; spell immunities and
 * resistances are not checked; only one ring around one zone at a time (overlapping zones
 * from different enemies can leave no candidate, then nothing is proposed); the melee record
 * holds one area (two areas stepped out of in turn re-arm each other).
 */

#ifndef MANGOS_COMMANDER_POSITIONING_BEHAVIOR_H
#define MANGOS_COMMANDER_POSITIONING_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PositioningBehavior : public IBehavior
    {
    public:
        static constexpr float ENTER_MARGIN = 0.5f;         // yd inside the zone before moving
        static constexpr float RING_EXTRA = 3.0f;           // yd outside the zone radius
        static constexpr float MELEE_RING_EXTRA = 0.5f;     // inner ring for melee
        static constexpr uint32 RING_POINTS = 12;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 4;    // GetHeight + up to 2 LOS each
        static constexpr uint32 MAX_ZONES_TRIED = 3;

        static constexpr float HEAL_RANGE = 28.0f;          // healer to holder (heals are 40, 30 for some)
        static constexpr float RANGED_RANGE = 30.0f;        // conservative spell / shot range
        static constexpr float MAX_STEP_Z = 4.0f;           // ground height change accepted
        static constexpr float LOS_EYE_HEIGHT = 2.0f;

        static constexpr float HORIZON_SEC = 6.0f;
        static constexpr float MAX_HORIZON_SEC = 20.0f;
        static constexpr float DODGE_SLACK_SEC = 0.3f;      // out this long before the cast ends
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;
        static constexpr float MELEE_AWAY_MAX_SEC = 4.0f;   // melee out of reach (no dodge): longest hold
        static constexpr uint32 MELEE_RETURN_MS = 3000;     // after that hold: time to come back in

        char const* GetId() const override { return "position"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
