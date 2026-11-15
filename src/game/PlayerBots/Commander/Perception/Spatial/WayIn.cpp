#include "Commander/Perception/Spatial/WayIn.h"
#include "Commander/CommanderMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Map.h"
#include "Opcodes.h"
#include "Timer.h"
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace Commander
{
namespace WayIn
{
    namespace
    {
        struct Record
        {
            uint32 mapId = 0;
            uint32 instanceId = 0;
            Way way;
            Position3 last;             // position of the previous packet
            bool hasLast = false;
            Position3 lastGround;       // the last packet's position on the ground
            bool hasLastGround = false;
            bool airborne = false;
            Position3 takeoff;          // last ground position before the air
            bool pendingGap = false;    // the next point's link is a gap
            uint32 seenMs = 0;
        };

        uint32 const PRUNE_EVERY = 5000;

        std::mutex s_lock;
        std::unordered_map<uint64, Record> s_records;
        uint32 s_sincePrune = 0;

        float Dist2dSq(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x;
            float const dy = a.y - b.y;
            return dx * dx + dy * dy;
        }

        float Dist3d(Position3 const& a, Position3 const& b)
        {
            float const dz = a.z - b.z;
            return std::sqrt(Dist2dSq(a, b) + dz * dz);
        }

        void Restart(Record& r, Player const* mover, Position3 const& pos, uint32 nowMs)
        {
            r.way = Way();
            r.way.startedMs = nowMs;
            r.hasLast = false;
            r.hasLastGround = false;
            r.airborne = false;
            r.pendingGap = false;
            if (AreaTriggerTeleport const* in = sObjectMgr.GetMapEntranceTrigger(mover->GetMapId()))
            {
                Position3 door;
                door.x = in->destination.x;
                door.y = in->destination.y;
                door.z = in->destination.z;
                r.way.startToEntranceYd = Dist3d(door, pos);
                r.way.fromEntrance = r.way.startToEntranceYd <= ENTRANCE_YD;
            }
        }

        // Adds `pos`, or cuts the loop back to an older point close to it (header "loops").
        void Push(Record& r, Position3 const& pos, bool oneWay)
        {
            std::vector<WayPoint>& pts = r.way.points;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                if (Dist2dSq(pts[i].pos, pos) <= LOOP_YD * LOOP_YD && std::fabs(pts[i].pos.z - pos.z) <= LOOP_DZ)
                {
                    pts.resize(i + 1);
                    r.pendingGap = false;
                    return;
                }
            }
            WayPoint p;
            p.pos = pos;
            p.oneWay = oneWay;
            p.gap = r.pendingGap;
            r.pendingGap = false;
            pts.push_back(p);
            if (pts.size() > MAX_POINTS)
            {
                pts.erase(pts.begin());
                r.way.fromEntrance = false;
            }
        }
    }

    void OnMove(Player const* mover, uint16 opcode)
    {
        if (!mover || !sCommanderMgr.IsEnabled() || !mover->GetGroup() || !mover->IsInWorld())
            return;
        Map const* map = mover->GetMap();
        if (!map || !map->IsDungeon())
            return;
        uint32 const nowMs = WorldTimer::getMSTime();
        MovementInfo const& info = mover->m_movementInfo;
        Position3 pos;
        pos.x = mover->GetPositionX();
        pos.y = mover->GetPositionY();
        pos.z = mover->GetPositionZ();
        bool const inAir = info.HasMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR);
        bool const swimming = info.HasMovementFlag(MOVEFLAG_SWIMMING);

        std::lock_guard<std::mutex> guard(s_lock);
        Record& r = s_records[mover->GetObjectGuid().GetRawValue()];
        r.seenMs = nowMs;

        // (erasing other records keeps `r` valid)
        if (++s_sincePrune >= PRUNE_EVERY)
        {
            s_sincePrune = 0;
            for (auto itr = s_records.begin(); itr != s_records.end();)
            {
                if (WorldTimer::getMSTimeDiff(itr->second.seenMs, nowMs) > KEEP_MS)
                    itr = s_records.erase(itr);
                else
                    ++itr;
            }
        }

        if (r.mapId != mover->GetMapId() || r.instanceId != mover->GetInstanceId())
        {
            r.mapId = mover->GetMapId();
            r.instanceId = mover->GetInstanceId();
            Restart(r, mover, pos, nowMs);
        }
        if (mover->GetTransport() || info.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
        {
            Restart(r, mover, pos, nowMs);
            return;
        }
        if (r.hasLast && !r.airborne && !inAir)
        {
            float const jumpSq = Dist2dSq(r.last, pos);
            if (jumpSq > TELEPORT_YD * TELEPORT_YD)
                Restart(r, mover, pos, nowMs);      // teleport, summon, zoned in again
            else if (jumpSq > BREAK_YD * BREAK_YD)
                r.pendingGap = true;                // Blink, knockback
        }
        r.last = pos;
        r.hasLast = true;

        if (!r.airborne && (opcode == MSG_MOVE_JUMP || inAir))
        {
            // Jumped: this packet is where it left the ground. Walked off: the last packet on
            // the ground is the edge.
            r.airborne = true;
            r.takeoff = opcode == MSG_MOVE_JUMP || !r.hasLastGround ? pos : r.lastGround;
            return;
        }
        if (r.airborne)
        {
            if (opcode != MSG_MOVE_FALL_LAND && inAir && !swimming)
                return;
            r.airborne = false;
            if (r.takeoff.z - pos.z > CLIMB_YD)
            {
                Push(r, r.takeoff, false);
                Push(r, pos, true);
            }
            r.lastGround = pos;
            r.hasLastGround = true;
            return;
        }
        if (inAir)
            return;
        r.lastGround = pos;
        r.hasLastGround = true;
        std::vector<WayPoint> const& pts = r.way.points;
        if (pts.empty() || Dist2dSq(pts.back().pos, pos) >= POINT_SPACING_YD * POINT_SPACING_YD ||
            std::fabs(pts.back().pos.z - pos.z) >= POINT_SPACING_YD)
            Push(r, pos, false);
    }

    Way Copy(ObjectGuid player, uint32 mapId, uint32 instanceId)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        auto const found = s_records.find(player.GetRawValue());
        if (found == s_records.end() || found->second.mapId != mapId || found->second.instanceId != instanceId)
            return Way();
        return found->second.way;
    }

    float LengthBack(Way const& way, size_t index)
    {
        float sum = 0.0f;
        for (size_t i = std::min(index, way.points.empty() ? size_t(0) : way.points.size() - 1); i > 0; --i)
            sum += Dist3d(way.points[i].pos, way.points[i - 1].pos);
        return sum;
    }

    WayBack Back(Way const& way, Position3 const& from)
    {
        WayBack out;
        float best = JOIN_YD * JOIN_YD;
        for (size_t i = 0; i < way.points.size(); ++i)
        {
            Position3 const& p = way.points[i].pos;
            if (std::fabs(p.z - from.z) > JOIN_DZ)
                continue;
            float const d = Dist2dSq(p, from);
            if (d <= best)      // ties: the later point (the nearer the walker came from)
            {
                best = d;
                out.found = true;
                out.joinIndex = i;
            }
        }
        if (!out.found)
            return out;
        out.joinYd = Dist3d(way.points[out.joinIndex].pos, from);
        out.lengthYd = out.joinYd + LengthBack(way, out.joinIndex);
        for (size_t i = out.joinIndex; i > 0; --i)
        {
            if (way.points[i].oneWay)
                ++out.oneWays;
            if (way.points[i].gap)
                ++out.gaps;
        }
        return out;
    }
}
}
