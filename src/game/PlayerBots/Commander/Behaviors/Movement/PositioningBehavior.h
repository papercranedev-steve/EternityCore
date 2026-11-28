/*
 * PartyBot Commander layer 3: step out of enemy areas ("position" module).
 * Design: .kiro/docs/2026-11-01-positioning.md. Zones: Perception/Spatial/HazardZones.h.
 *
 * Who: healers and ranged damage (control and damage areas), melee damage (only Pulse areas
 * and Burst spells whose damage is a periodic aura, e.g. Melt Ore). Tanks only for the areas
 * an ability sheet says everyone leaves, while they are cast (HAZARD_TANK, Doan's Detonation);
 * they move like melee (reach first, the melee out-of-reach record).
 *
 * When: the bot stands at least ENTER_MARGIN inside a zone dangerous to its role. Nothing while
 * the retreat runs (RetreatBehavior owns movement at Override tier), during the pull's
 * Approach / Return phases, out of combat, rooted, or while already walking under a movement
 * hold (a MoveTo of this or another module in progress).
 *
 * Where (since 2026-11-10, position field step B): cells of the position field
 * (WorldModel::field, FieldSpots::Collect) in a band around the zone centre from zone radius
 * (spell radius + 1) + RING_EXTRA (melee: + MELEE_RING_EXTRA, to stay in reach of a large
 * target) out to zone radius + RING_EXTRA + SEARCH_BAND_YD. The field already drops cells off
 * the ground or cut off from the party, cells an idle creature covers, cells a patrol reaches
 * before the fight ends and cells the bot cannot walk to around idle creatures; a long slope
 * stays usable (the ring before compared heights with the bot's own). A cell is kept when:
 *  - it is outside every zone dangerous to this role (second pass: Range zones may stay);
 *  - healer: the holder (tank bot, or whoever most enemies are hitting) is within HEAL_RANGE;
 *    ranged: the current victim is within RANGED_RANGE.
 * Shortest grid walk first (melee: cells that keep melee reach first). Live checks on at most
 * MAX_GEOMETRY_CHECKS cells per tick: no idle creature attacks this bot there (its own scan),
 * the holder / victim in line of sight, SafeRoute::PathIsSafe (detour MAX_PATH_DETOUR, out of
 * an idle circle the bot stands in: LeavingFrom). No field: nothing is proposed.
 *
 * Value (points, value-scoring doc): benefit - cost. Hits and their points are
 * Perception/Values/SpotCost.h (ZoneHits, Dodges, AddZoneLoss); the horizon below is this module's.
 *  hits(zone) over H = min(max(HORIZON_SEC, repeat delay), MAX_HORIZON_SEC, fight remaining):
 *    Pulse        H / period
 *    Burst/Range  H / repeat delay; while the enemy casts it: 1 + (H - remaining) / repeat
 *                 when the bot gets out before the cast ends (walk + SpotCost::DODGE_SLACK_SEC <= remaining),
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
 * (TeamBotAI::m_hazardGiveUp: zone enemy + spell); after its hold the chase takes the bot
 * back in, and while it stays inside that area no more out-of-reach steps are proposed
 * (dodges, steps that keep reach and Urgent / lethal steps still are). Re-armed when the bot
 * leaves the area again, when the area is gone, or when the bot is not back within
 * MELEE_RETURN_MS after the hold. Before: a hold of up to MAX_HORIZON_SEC, repeated after
 * every chase back in, kept the melee out of the fight.
 *
 * Jitter: proposes only ENTER_MARGIN inside; the target ring is RING_EXTRA outside, so a bot
 * needs to drift back ~3.5 yd before the next move. Apart from the melee record above there is
 * no "just moved" timer; the margins are the hysteresis. Healers and ranged do not follow the
 * leader during the party's fight (TeamBotAI::IsStanceControlled), so they are not walked
 * straight back in after the hold.
 *
 * Runs while the party fights (WorldModel::InFight), also for a bot not in combat itself.
 *
 * Timing: a refresh that sees the bot inside such a zone wakes it (WantsTickNow); a bot busy
 * with its own cast stops it when staying until the cast ends costs more (CastBreakBenefit).
 *
 * Known limits: cells are 2 yd (the spot is a cell centre); zones are
 * from the last refresh (moving casters); the chase (ranged at 25 yd, melee) may still walk
 * the bot back in after the hold, the next tick then moves it out again; spell immunities and
 * resistances are not checked; only one band around one zone at a time (overlapping zones
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
        static constexpr float SEARCH_BAND_YD = 6.0f;       // width of the band of cells looked at
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 4;    // a path and up to 2 LOS each
        static constexpr float MAX_PATH_DETOUR = 2.0f;
        static constexpr uint32 MAX_ZONES_TRIED = 3;

        static constexpr float HEAL_RANGE = 28.0f;          // healer to holder (heals are 40, 30 for some)
        static constexpr float RANGED_RANGE = 30.0f;        // conservative spell / shot range
        static constexpr float LOS_EYE_HEIGHT = 2.0f;

        static constexpr float HORIZON_SEC = 6.0f;
        static constexpr float MAX_HORIZON_SEC = 20.0f;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;
        static constexpr float MELEE_AWAY_MAX_SEC = 4.0f;   // melee out of reach (no dodge): longest hold
        static constexpr uint32 MELEE_RETURN_MS = 3000;     // after that hold: time to come back in

        char const* GetId() const override { return "position"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        // The bot stands ENTER_MARGIN inside a zone dangerous to its role (facts: its last position).
        bool WantsTickNow(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        // Own cast, standing ENTER_MARGIN inside such zones (not one whose enemy chases the bot):
        // hits taken by staying `castLeftMs` longer before walking out. A cast in progress: its
        // hit when walking out now gets the bot out in time and after the own cast it does not.
        // Otherwise castLeft / repeat delay hits. Plus the death penalty when they would kill it.
        // Walk = distance to the zone edge + RING_EXTRA. 0 when Propose would find no spot to
        // step to (the same search): an area that fills the room does not stop casts.
        float CastBreakBenefit(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               uint32 castLeftMs, char const*& reason) const override;
    };
}

#endif
