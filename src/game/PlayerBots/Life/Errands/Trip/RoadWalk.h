/*
 * Bot daily life (PlayerBots/Life): walking a long way along a line of points (a road of
 * Commander/Perception/Spatial/RoadNetwork), as a player does: always heading for a point a bit
 * ahead on the line, the server's pathfinding for the steps in between. Used by the population's
 * road travellers (Population/TravellerPopulation.h, ambient flow doc §3.2).
 *
 * The walk is the bot's progress along the line (never backwards: a bot pushed aside or back
 * from a fight goes on from where it got to). A new leg is sent when the bot is near the point
 * it heads for or stopped (a fight, eating in between: Update again afterwards and it goes on).
 * Stuck: no progress for STUCK_MS.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_ROAD_WALK_H
#define MANGOS_LIFE_TRIP_ROAD_WALK_H

#include "Common.h"
#include "Commander/Perception/Spatial/RoadNetwork.h"
#include <vector>

class Player;

namespace Life
{
namespace Trip
{
    struct RoadWalkState
    {
        std::vector<Commander::Roads::RoadPoint> line;
        std::vector<float> along;       // 2d length from line[0]
        float progress = 0.0f;          // how far along the bot got
        bool run = true;
        bool heading = false;           // a leg was sent to (tx, ty, tz)
        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        float progressAtCheck = 0.0f;
        uint32 checkMs = 0;
        uint32 lastUpdateMs = 0;        // Update not called for a while (a fight): going on afresh
    };

    enum class RoadWalkResult : uint8
    {
        Walking,
        Arrived,
        Stuck,
    };

    namespace RoadWalk
    {
        // Heads for the point this far ahead on the line.
        constexpr float LOOKAHEAD_YD = 45.0f;
        // A new leg when the bot is this close to the point it heads for.
        constexpr float RELEG_YD = 15.0f;
        constexpr float ARRIVE_YD = 3.0f;
        constexpr uint32 STUCK_MS = 25 * IN_MILLISECONDS;
        // Update not called for longer than this: the walk was interrupted.
        constexpr uint32 RESUME_GAP_MS = 3 * IN_MILLISECONDS;

        void Begin(RoadWalkState& state, std::vector<Commander::Roads::RoadPoint> line, bool run, uint32 now);
        RoadWalkResult Update(Player* me, RoadWalkState& state, uint32 now);
        // The way left to go (yards).
        float Left(RoadWalkState const& state);
        // Stops walking (the next Update sends a new leg).
        void Pause(Player* me, RoadWalkState& state);
    }
}
}

#endif
