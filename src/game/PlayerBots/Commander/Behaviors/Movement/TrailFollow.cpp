#include "Commander/Behaviors/Movement/TrailFollow.h"
#include "Commander/BotDirectives.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "PathFinder.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "Log.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Commander
{
namespace TrailFollow
{
    namespace
    {
        float Dist(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x;
            float const dy = a.y - b.y;
            float const dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        Position3 PosOf(Unit const* u)
        {
            Position3 p;
            p.x = u->GetPositionX();
            p.y = u->GetPositionY();
            p.z = u->GetPositionZ();
            return p;
        }

        Movement::Vector3 V(Position3 const& p)
        {
            return Movement::Vector3(p.x, p.y, p.z);
        }

        bool Reached(uint32 nowMs, uint32 atMs)
        {
            return int32(nowMs - atMs) >= 0;
        }

        bool InAir(Unit const* u)
        {
            return u->m_movementInfo.HasMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR);
        }

        // One spline from where the bot is; the MotionMaster stays idle under it.
        // `path[0]` is replaced by the current position (MoveSplineInit::Launch). 0: not launched.
        int32 Launch(Player* me, Movement::PointsArray const& path, bool fall)
        {
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
            Movement::MoveSplineInit init(*me, "TrailFollow");
            init.MovebyPath(path);
            init.SetWalk(false);
            if (fall)
                init.SetFall();
            return init.Launch();
        }

        int32 LaunchApproach(Player* me, Position3 const& to)
        {
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
            Movement::MoveSplineInit init(*me, "TrailFollow");
            init.MoveTo(to.x, to.y, to.z, MOVE_PATHFINDING);
            init.SetWalk(false);
            return init.Launch();
        }

        void End(PartyBotAI* bot, TrailFollowState& state, bool stopMoving, char const* why)
        {
            Player* me = bot->me;
            uint32 const now = WorldTimer::getMSTime();
            if (stopMoving && state.active && me->IsInWorld())
            {
                if (!me->IsStopped())
                    me->StopMoving();
                me->GetMotionMaster()->Clear(false, true);
                me->GetMotionMaster()->MoveIdle();
            }
            BotDirectives& directives = bot->Directives();
            char const* owner = directives.GetMovementOwner(now);
            if (owner && std::strcmp(owner, SOURCE) == 0)
                directives.ReleaseMovement();
            if (state.active)
                sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[PartyBot Commander] %s trail walk ends: %s", me->GetName(), why);
            state.active = false;
            state.route.clear();
            state.next = 0;
            state.leg = TrailFollowState::Leg::None;
            state.nextCheckMs = now + CHECK_EVERY_MS;
        }

        // The next leg from state.next on. False: the route is done or a leg did not launch.
        bool NextLeg(PartyBotAI* bot, TrailFollowState& state)
        {
            Player* me = bot->me;
            Position3 const here = PosOf(me);
            std::vector<TrailPoint> const& route = state.route;

            while (state.next < route.size())
            {
                TrailPoint const& p = route[state.next];
                Movement::PointsArray path;
                path.push_back(V(here));

                // A jump or fall: from its Takeoff (where the bot is now) to its Land.
                if (p.step == TrailStep::Land && state.next > 0 && route[state.next - 1].step == TrailStep::Takeoff)
                {
                    bool const fall = route[state.next - 1].pos.z - p.pos.z >= FALL_MIN_DROP_YD;
                    path.push_back(V(p.pos));
                    ++state.next;
                    int32 const ms = Launch(me, path, fall);
                    if (ms <= 0)
                        return false;
                    state.leg = TrailFollowState::Leg::Fall;
                    state.legEnd = p.pos;
                    // Nothing starts a follow or a chase in the air.
                    bot->Directives().HoldMovement(WorldTimer::getMSTime(), uint32(ms) + 300, SOURCE);
                    return true;
                }

                // A run through the recorded points, up to and with the next Takeoff.
                Position3 last = here;
                while (state.next < route.size() && path.size() <= MAX_LEG_POINTS)
                {
                    TrailPoint const& q = route[state.next];
                    if (q.step == TrailStep::Land && path.size() > 1)
                        break;      // (after a Takeoff: the loop ended there)
                    ++state.next;
                    if (Dist(last, q.pos) >= 0.5f)
                    {
                        path.push_back(V(q.pos));
                        last = q.pos;
                    }
                    if (q.step == TrailStep::Takeoff)
                        break;
                }
                if (path.size() < 2)
                    continue;       // already there (a Takeoff under the bot): the fall next
                if (Launch(me, path, false) <= 0)
                    return false;
                state.leg = TrailFollowState::Leg::Run;
                state.legEnd = last;
                return true;
            }
            return false;
        }

        // Navmesh length of the walk from `me` to `to`; infinite without a path there. Without a
        // navmesh (PATHFIND_NOT_USING_PATH) the generators walk straight: the straight line.
        float NavmeshLength(Player* me, Position3 const& to)
        {
            PathInfo path(me);
            path.calculate(to.x, to.y, to.z);
            uint32 const type = path.getPathType();
            if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
                return std::numeric_limits<float>::infinity();
            if (type & PATHFIND_NOT_USING_PATH)
                return Dist(PosOf(me), to);
            return path.Length();
        }

        // The walk to an entry point; negative when no navmesh path reaches it.
        float EntryWalk(Player* me, Position3 const& to)
        {
            float const straight = Dist(PosOf(me), to);
            if (straight <= ON_TRAIL_YD)
                return straight;
            PathInfo path(me);
            path.calculate(to.x, to.y, to.z);
            if (path.getPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH))
                return -1.0f;
            if (!SafeRoute::PathReaches(me->GetMap(), path, to))
                return -1.0f;
            return path.Length();
        }

        // The detour the navmesh path makes is worth the trail way.
        bool Worth(float navmesh, float trailWay)
        {
            return navmesh > trailWay * DETOUR_RATIO && navmesh - trailWay > DETOUR_MIN_SAVED_YD;
        }
    }

    bool Consider(PartyBotAI* bot, TrailFollowState& state, Player const* leader)
    {
        if (state.active)
            return true;
        Player* me = bot->me;
        uint32 const now = WorldTimer::getMSTime();
        if (!Reached(now, state.nextCheckMs))
            return false;
        state.nextCheckMs = now + CHECK_EVERY_MS;

        if (!leader || !leader->IsAlive() || leader->GetTransport() || leader->IsTaxiFlying() || InAir(leader) ||
            me->GetTransport() || InAir(me) || me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) ||
            bot->IsBackpedaling() || bot->IsJumpCasting())
            return false;
        Position3 const mePos = PosOf(me);
        Position3 const leaderPos = PosOf(leader);
        if (Dist(mePos, leaderPos) < CHECK_FROM_YD)
            return false;

        std::vector<TrailPoint> const trail = LeaderTrail::Copy(leader->GetObjectGuid(), me->GetMapId(), me->GetInstanceId());
        if (trail.size() < 2)
            return false;
        float const tail = Dist(trail.back().pos, leaderPos);
        if (tail > TRAIL_END_YD)
            return false;

        // The trail left from each point to its end.
        std::vector<float> remain(trail.size(), 0.0f);
        for (size_t i = trail.size() - 1; i-- > 0;)
            remain[i] = remain[i + 1] + Dist(trail[i].pos, trail[i + 1].pos);

        // Entry points by straight distance + trail left (a lower bound of the way).
        struct Entry
        {
            size_t index;
            float estimate;
        };
        std::vector<Entry> entries;
        for (size_t i = 0; i < trail.size(); ++i)
        {
            float const d = Dist(mePos, trail[i].pos);
            if (d <= ENTRY_RADIUS_YD)
                entries.push_back({ i, d + remain[i] });
        }
        if (entries.empty())
            return false;
        std::sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) { return a.estimate < b.estimate; });

        float const navmesh = NavmeshLength(me, leaderPos);
        if (!Worth(navmesh, entries.front().estimate + tail))
            return false;

        size_t best = 0;
        float bestWalk = 0.0f;
        float bestWay = std::numeric_limits<float>::infinity();
        for (size_t k = 0; k < entries.size() && k < MAX_ENTRY_CHECKS; ++k)
        {
            Entry const& e = entries[k];
            if (e.estimate + tail >= bestWay)
                break;
            float const walk = EntryWalk(me, trail[e.index].pos);
            if (walk < 0.0f)
                continue;
            float const way = walk + remain[e.index] + tail;
            if (way < bestWay)
            {
                bestWay = way;
                best = e.index;
                bestWalk = walk;
            }
        }
        if (!std::isfinite(bestWay) || !Worth(navmesh, bestWay))
            return false;

        // Lava on the way: only with the health to cross it. The leader may heal on the other side.
        if (me->GetHealthPercent() < MAGMA_MIN_HEALTH_PCT)
            for (size_t i = best; i < trail.size(); ++i)
                if (trail[i].magma)
                    return false;

        state.route.assign(trail.begin() + best, trail.end());
        state.next = 0;
        state.leader = leader->GetObjectGuid();
        state.active = true;
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        state.deadlineMs = now + uint32(2.0f * bestWay / speed * 1000.0f) + 5000;

        bool launched;
        if (bestWalk > ON_TRAIL_YD)
        {
            launched = LaunchApproach(me, state.route.front().pos) > 0;
            state.next = 1;
            state.leg = TrailFollowState::Leg::Approach;
            state.legEnd = state.route.front().pos;
        }
        else
            launched = NextLeg(bot, state);

        if (std::isfinite(navmesh))
            sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[PartyBot Commander] %s follows %s's trail: %u points, %.0f yd (navmesh path %.0f yd)",
                me->GetName(), leader->GetName(), uint32(state.route.size()), bestWay, navmesh);
        else
            sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[PartyBot Commander] %s follows %s's trail: %u points, %.0f yd (no navmesh path)",
                me->GetName(), leader->GetName(), uint32(state.route.size()), bestWay);

        if (!launched)
        {
            End(bot, state, true, "the first leg did not launch");
            return false;
        }
        return true;
    }

    void Update(PartyBotAI* bot, TrailFollowState& state)
    {
        if (!state.active)
            return;
        Player* me = bot->me;
        if (!me->IsInWorld() || me->IsBeingTeleported() || !me->IsAlive() ||
            me->HasUnitState(UNIT_STATE_NO_FREE_MOVE) || me->GetTransport())
        {
            End(bot, state, false, "cannot move freely");
            return;
        }
        // Another movement generator or a Commander walk (Backpedal, jump turn) took the bot.
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE ||
            bot->IsBackpedaling() || bot->IsJumpCasting())
        {
            End(bot, state, false, "other movement");
            return;
        }

        uint32 const now = WorldTimer::getMSTime();
        bool const legRunning = !me->movespline->Finalized();
        // (in the air the fall ends first)
        if (!(legRunning && state.leg == TrailFollowState::Leg::Fall))
        {
            char const* owner = bot->Directives().GetMovementOwner(now);
            if (owner && std::strcmp(owner, SOURCE) != 0)
            {
                End(bot, state, false, "a module walk");
                return;
            }
            if (me->GetVictim() || bot->IsStanceControlled())
            {
                End(bot, state, true, "fight");
                return;
            }
            if (Reached(now, state.deadlineMs))
            {
                End(bot, state, true, "too long");
                return;
            }
        }
        if (legRunning)
            return;

        if (Dist(PosOf(me), state.legEnd) > LEG_END_YD)
        {
            End(bot, state, true, "not at the leg's end");
            return;
        }
        if (!NextLeg(bot, state))
            End(bot, state, false, state.next >= state.route.size() ? "trail done" : "a leg did not launch");
    }

    bool IsFalling(Player const* me, TrailFollowState const& state)
    {
        return state.active && state.leg == TrailFollowState::Leg::Fall && !me->movespline->Finalized();
    }

    void Stop(PartyBotAI* bot, TrailFollowState& state)
    {
        if (state.active)
            End(bot, state, true, "player command");
    }
}
}
