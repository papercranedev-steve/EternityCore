/*
 * PartyBot Commander layer 3 (cerebellum) with its hands: follow the leader the way he went
 * (user, 2026-11-15). MoveFollow walks the navmesh path; after a shortcut the navmesh does not
 * know (the Gnomeregan drop to the lower level, the Blackrock Depths window jump into the lava)
 * it walks a long detour or finds no path, and out of combat the bot is teleported only at
 * 100 yd. The leader's recorded trail (Perception/Spatial/LeaderTrail.h) is copied instead.
 *
 * Consider (bot update, in place of the follow spot check while the bot follows: no victim,
 * no stance control, no movement hold), at most every CHECK_EVERY_MS, only when the bot is
 * CHECK_FROM_YD or more from the leader:
 *   - the trail ends within TRAIL_END_YD of the leader, the leader stands on the ground (not in
 *     the air, alive, no transport), neither is on a transport;
 *   - the navmesh path to the leader: none (NOPATH / INCOMPLETE) = length infinite;
 *   - the trail way: a walk to an entry point (trail points within ENTRY_RADIUS_YD, the
 *     MAX_ENTRY_CHECKS with the least straight distance + trail left checked with a navmesh
 *     path that reaches them) + the trail from there;
 *   - taken when the path is DETOUR_RATIO times the trail way and DETOUR_MIN_SAVED_YD longer;
 *   - not through lava (a point the leader stood in lava at) below MAGMA_MIN_HEALTH_PCT health:
 *     the bot waits (the leader may not be fit for the way he went: he may heal on the other
 *     side, or die there, then there is nothing to follow: the leader must be alive).
 * Walk (Update every frame): one spline per leg, MotionMaster idle, as a client would go:
 *   Approach  navmesh path to the entry point (when farther than ON_TRAIL_YD);
 *   Run       straight through the recorded Ground / Swim points up to the next Takeoff (at
 *             most MAX_LEG_POINTS), no pathfinding;
 *   Fall      Takeoff -> Land as a falling spline (MoveSplineInit::SetFall: the fall elevation),
 *             or straight when the landing is not lower (a jump across or up). Under a
 *             movement hold (owner SOURCE) for its duration: nothing starts a follow or chase
 *             in the air, and the bot update returns at once (IsFalling: a cast's StopMoving
 *             would leave it hanging). A spline takes no fall damage; the leader did.
 * Ends at the trail's end (the original follow goes on), when another movement generator or
 * module takes the bot, a victim or stance control comes (not in the air), the bot cannot move
 * freely, is not where the leg ended (LEG_END_YD), or after the walk's time twice + 5 s.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_TRAIL_FOLLOW_H
#define MANGOS_COMMANDER_TRAIL_FOLLOW_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/Spatial/LeaderTrail.h"
#include <vector>

class PartyBotAI;
class Player;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct TrailFollowState
    {
        enum class Leg : uint8 { None, Approach, Run, Fall };

        bool active = false;
        std::vector<TrailPoint> route;  // the trail from the entry point on
        size_t next = 0;                // the first route point no leg has gone to yet
        Leg leg = Leg::None;
        Position3 legEnd;
        ObjectGuid leader;
        uint32 nextCheckMs = 0;
        uint32 deadlineMs = 0;
    };

    namespace TrailFollow
    {
        constexpr char const* SOURCE = "trail";

        constexpr float CHECK_FROM_YD = 15.0f;
        constexpr uint32 CHECK_EVERY_MS = 1000;
        constexpr float TRAIL_END_YD = 8.0f;
        constexpr float ENTRY_RADIUS_YD = 30.0f;
        constexpr uint32 MAX_ENTRY_CHECKS = 4;
        constexpr float ON_TRAIL_YD = 2.0f;
        constexpr float DETOUR_RATIO = 1.5f;
        constexpr float DETOUR_MIN_SAVED_YD = 15.0f;
        constexpr float MAGMA_MIN_HEALTH_PCT = 60.0f;
        constexpr size_t MAX_LEG_POINTS = 40;      // MONSTER_MOVE packs offsets: keep legs short
        constexpr float LEG_END_YD = 3.0f;
        constexpr float FALL_MIN_DROP_YD = 1.0f;

        // Bot update while following. True: walking the trail (no MoveFollow, no follow spot check).
        bool Consider(PartyBotAI* bot, TrailFollowState& state, Player const* leader);

        // Every frame from PartyBotAI::UpdateAI.
        void Update(PartyBotAI* bot, TrailFollowState& state);

        // In a Fall leg's spline: the bot update does nothing (a cast stops the spline in the air).
        bool IsFalling(Player const* me, TrailFollowState const& state);

        // Player commands (PartyBotAI::ClearMovementHold): stops the walk where the bot is.
        void Stop(PartyBotAI* bot, TrailFollowState& state);
    }
}

#endif
