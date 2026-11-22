#include "Life/Errands/Trip/RoadWalk.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Life
{
namespace Trip
{
namespace RoadWalk
{
    using Commander::Roads::RoadPoint;

    namespace
    {
        // The bot's place on the line is looked for this far behind / ahead of its progress
        // (a road can pass the same spot twice: no jumping to the other pass).
        float const BACK_WINDOW_YD = 20.0f;
        float const AHEAD_WINDOW_YD = 90.0f;
        // Farther than this from the line: not on it (pushed away, a fight); progress waits.
        float const ON_LINE_YD = 25.0f;
        // A stuck check counts this much progress.
        float const PROGRESS_YD = 2.0f;

        float Dist2d(float ax, float ay, float bx, float by)
        {
            return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
        }

        RoadPoint PointAt(RoadWalkState const& state, float s)
        {
            std::vector<RoadPoint> const& line = state.line;
            if (s <= 0.0f)
                return line.front();
            for (size_t i = 1; i < line.size(); ++i)
            {
                if (s <= state.along[i])
                {
                    float const span = state.along[i] - state.along[i - 1];
                    float const t = span > 0.0f ? (s - state.along[i - 1]) / span : 0.0f;
                    return { line[i - 1].x + (line[i].x - line[i - 1].x) * t,
                             line[i - 1].y + (line[i].y - line[i - 1].y) * t,
                             line[i - 1].z + (line[i].z - line[i - 1].z) * t };
                }
            }
            return line.back();
        }

        // The bot's arc length on the line near its progress, -1: not near the line.
        float Locate(RoadWalkState const& state, float x, float y)
        {
            float best = -1.0f, bestDist = ON_LINE_YD;
            for (size_t i = 1; i < state.line.size(); ++i)
            {
                if (state.along[i] < state.progress - BACK_WINDOW_YD)
                    continue;
                if (state.along[i - 1] > state.progress + AHEAD_WINDOW_YD)
                    break;
                RoadPoint const& a = state.line[i - 1];
                RoadPoint const& b = state.line[i];
                float const dx = b.x - a.x, dy = b.y - a.y;
                float const len2 = dx * dx + dy * dy;
                float t = len2 > 0.0f ? ((x - a.x) * dx + (y - a.y) * dy) / len2 : 0.0f;
                t = std::min(1.0f, std::max(0.0f, t));
                float const d = Dist2d(x, y, a.x + dx * t, a.y + dy * t);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = state.along[i - 1] + t * (state.along[i] - state.along[i - 1]);
                }
            }
            return best;
        }
    }

    void Begin(RoadWalkState& state, std::vector<RoadPoint> line, bool run, uint32 now)
    {
        state = RoadWalkState();
        state.line = std::move(line);
        state.along.assign(state.line.size(), 0.0f);
        for (size_t i = 1; i < state.line.size(); ++i)
            state.along[i] = state.along[i - 1] + Dist2d(state.line[i - 1].x, state.line[i - 1].y, state.line[i].x, state.line[i].y);
        state.run = run;
        state.checkMs = now;
    }

    float Left(RoadWalkState const& state)
    {
        return state.along.empty() ? 0.0f : std::max(0.0f, state.along.back() - state.progress);
    }

    void Pause(Player* me, RoadWalkState& state)
    {
        if (state.heading && !me->IsStopped())
            me->StopMoving();
        state.heading = false;
    }

    RoadWalkResult Update(Player* me, RoadWalkState& state, uint32 now)
    {
        if (state.line.size() < 2)
            return RoadWalkResult::Arrived;

        // Not walked for a while (a fight, a meal, a mount cast): a new leg, the stuck clock anew.
        if (state.lastUpdateMs && WorldTimer::getMSTimeDiff(state.lastUpdateMs, now) > RESUME_GAP_MS)
            state.heading = false;
        state.lastUpdateMs = now;

        float const s = Locate(state, me->GetPositionX(), me->GetPositionY());
        if (s > state.progress)
            state.progress = s;

        RoadPoint const& end = state.line.back();
        if (Left(state) <= ARRIVE_YD || me->GetDistance2d(end.x, end.y, SizeFactor::None) <= ARRIVE_YD)
        {
            Pause(me, state);
            return RoadWalkResult::Arrived;
        }

        // (going on after a pause: the stuck clock starts again)
        if (!state.heading || state.progress >= state.progressAtCheck + PROGRESS_YD)
        {
            state.progressAtCheck = state.progress;
            state.checkMs = now;
        }
        else if (WorldTimer::getMSTimeDiff(state.checkMs, now) > STUCK_MS)
            return RoadWalkResult::Stuck;

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return RoadWalkResult::Walking;

        bool const closeToTarget = state.heading && me->GetDistance2d(state.tx, state.ty, SizeFactor::None) <= RELEG_YD;
        if (state.heading && me->IsMoving() && !closeToTarget)
            return RoadWalkResult::Walking;

        RoadPoint target = PointAt(state, std::min(state.along.back(), state.progress + LOOKAHEAD_YD));
        // On the ground there (the line's z is the navmesh's corner height in between).
        float const ground = me->GetMap()->GetHeight(target.x, target.y, target.z + 5.0f);
        if (ground > INVALID_HEIGHT && std::fabs(ground - target.z) < 10.0f)
            target.z = ground;
        state.tx = target.x;
        state.ty = target.y;
        state.tz = target.z;
        state.heading = true;
        uint32 const options = MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES | (state.run ? MOVE_RUN_MODE : MOVE_WALK_MODE);
        me->GetMotionMaster()->MovePoint(0, target.x, target.y, target.z, options);
        return RoadWalkResult::Walking;
    }
}
}
}
