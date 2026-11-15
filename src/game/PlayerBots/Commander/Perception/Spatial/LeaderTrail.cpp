#include "Commander/Perception/Spatial/LeaderTrail.h"
#include "Commander/CommanderMgr.h"
#include "Player.h"
#include "Opcodes.h"
#include "Timer.h"
#include <deque>
#include <mutex>
#include <unordered_map>

namespace Commander
{
namespace LeaderTrail
{
    namespace
    {
        struct Record
        {
            uint32 mapId = 0;
            uint32 instanceId = 0;
            std::deque<TrailPoint> points;
            Position3 last;             // position of the previous packet
            bool hasLast = false;
            Position3 lastGround;       // the previous packet's position, if it was on the ground
            bool hasLastGround = false;
            bool airborne = false;
            uint32 seenMs = 0;
        };

        // Players without a packet for KEEP_MS are removed every PRUNE_EVERY packets.
        uint32 const PRUNE_EVERY = 5000;

        std::mutex s_lock;
        std::unordered_map<uint64, Record> s_records;
        uint32 s_sincePrune = 0;

        float DistSq(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x;
            float const dy = a.y - b.y;
            float const dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        }

        void Restart(Record& r)
        {
            r.points.clear();
            r.hasLast = false;
            r.hasLastGround = false;
            r.airborne = false;
        }

        void Push(Record& r, Position3 const& pos, TrailStep step, bool magma, uint32 nowMs)
        {
            TrailPoint p;
            p.pos = pos;
            p.timeMs = nowMs;
            p.step = step;
            p.magma = magma;
            r.points.push_back(p);
            while (r.points.size() > MAX_POINTS)
                r.points.pop_front();
        }

        void DropOld(Record& r, uint32 nowMs)
        {
            while (!r.points.empty() && WorldTimer::getMSTimeDiff(r.points.front().timeMs, nowMs) > KEEP_MS)
                r.points.pop_front();
        }
    }

    void OnMove(Player const* mover, uint16 opcode)
    {
        if (!mover || !sCommanderMgr.IsEnabled() || !mover->GetGroup())
            return;
        uint32 const nowMs = WorldTimer::getMSTime();
        MovementInfo const& info = mover->m_movementInfo;
        Position3 pos;
        pos.x = mover->GetPositionX();
        pos.y = mover->GetPositionY();
        pos.z = mover->GetPositionZ();
        bool const inAir = info.HasMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR);
        bool const swimming = info.HasMovementFlag(MOVEFLAG_SWIMMING);
        bool const magma = mover->IsInMagma();

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
            Restart(r);
            r.mapId = mover->GetMapId();
            r.instanceId = mover->GetInstanceId();
        }
        // The follow point on a transport moves with it: no trail there.
        if (mover->GetTransport() || info.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
        {
            Restart(r);
            return;
        }
        // A jump on the ground nobody can walk (teleport, Blink, summon).
        if (r.hasLast && !r.airborne && !inAir && DistSq(r.last, pos) > BREAK_YD * BREAK_YD)
            Restart(r);
        r.last = pos;
        r.hasLast = true;

        DropOld(r, nowMs);

        if (!r.airborne && (opcode == MSG_MOVE_JUMP || inAir))
        {
            // Jumped: this packet is sent where it left the ground. Walked off: the last packet
            // on the ground is the edge.
            r.airborne = true;
            if (opcode == MSG_MOVE_JUMP)
                Push(r, pos, TrailStep::Takeoff, magma, nowMs);
            else if (r.hasLastGround)
                Push(r, r.lastGround, TrailStep::Takeoff, magma, nowMs);
            else
                r.airborne = false;     // no takeoff known: wait for the ground again
            r.hasLastGround = false;
            if (r.airborne)
                return;
        }

        if (r.airborne)
        {
            if (opcode != MSG_MOVE_FALL_LAND && inAir && !swimming)
                return;
            r.airborne = false;
            Push(r, pos, TrailStep::Land, magma, nowMs);
            r.lastGround = pos;
            r.hasLastGround = true;
            return;
        }

        if (inAir)
            return;
        r.lastGround = pos;
        r.hasLastGround = true;
        if (r.points.empty() || DistSq(r.points.back().pos, pos) >= POINT_SPACING_YD * POINT_SPACING_YD)
            Push(r, pos, swimming ? TrailStep::Swim : TrailStep::Ground, magma, nowMs);
    }

    std::vector<TrailPoint> Copy(ObjectGuid player, uint32 mapId, uint32 instanceId)
    {
        std::vector<TrailPoint> out;
        uint32 const nowMs = WorldTimer::getMSTime();
        std::lock_guard<std::mutex> guard(s_lock);
        auto const found = s_records.find(player.GetRawValue());
        if (found == s_records.end() || found->second.mapId != mapId || found->second.instanceId != instanceId)
            return out;
        Record& r = found->second;
        DropOld(r, nowMs);
        out.assign(r.points.begin(), r.points.end());
        // Still in the air: the last Takeoff has no Land yet.
        if (r.airborne && !out.empty() && out.back().step == TrailStep::Takeoff)
            out.pop_back();
        return out;
    }
}
}
