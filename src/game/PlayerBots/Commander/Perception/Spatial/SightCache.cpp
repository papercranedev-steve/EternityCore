#include "Commander/Perception/Spatial/SightCache.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Unit.h"
#include "Map.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        uint64 PairKey(int32 fx, int32 fy, int32 tx, int32 ty)
        {
            return (uint64(uint16(fx)) << 48) | (uint64(uint16(fy)) << 32) | (uint64(uint16(tx)) << 16) | uint64(uint16(ty));
        }

        bool Line(Unit const* viewer, float fx, float fy, float fz, float tx, float ty, float tz)
        {
            return viewer->GetMap()->isInLineOfSight(fx, fy, fz + viewer->GetCollisionHeight(),
                                                     tx, ty, tz + SightCache::TARGET_HEIGHT);
        }
    }

    bool SightCache::Live(Unit const* viewer, Position3 const& from, Position3 const& to)
    {
        return Line(viewer, from.x, from.y, from.z, to.x, to.y, to.z);
    }

    bool SightCache::LiveFromCell(Unit const* viewer, Position3 const& cell, Position3 const& to)
    {
        float z = cell.z;
        float const ground = viewer->GetMap()->GetHeight(cell.x, cell.y, cell.z + STAND_Z_TOLERANCE);
        if (ground > INVALID_HEIGHT && std::fabs(ground - cell.z) <= STAND_Z_TOLERANCE)
            z = ground;
        return Line(viewer, cell.x, cell.y, z, to.x, to.y, to.z);
    }

    SightCache::Sight SightCache::Cells(Unit const* viewer, Position3 const& from, Position3 const& to, uint32& budget)
    {
        if (viewer->GetMapId() != m_mapId)
        {
            m_entries.clear();
            m_mapId = viewer->GetMapId();
        }
        int32 const fx = PositionField::CellOf(from.x), fy = PositionField::CellOf(from.y);
        int32 const tx = PositionField::CellOf(to.x), ty = PositionField::CellOf(to.y);
        uint8 const eye = uint8(std::min(255.0f, viewer->GetCollisionHeight() / EYE_STEP + 0.5f));
        uint32 const now = WorldTimer::getMSTime();
        uint64 const key = PairKey(fx, fy, tx, ty);
        auto const it = m_entries.find(key);
        if (it != m_entries.end())
        {
            Entry const& e = it->second;
            if (e.eye == eye && std::fabs(e.fromZ - from.z) <= Z_TOLERANCE && std::fabs(e.toZ - to.z) <= Z_TOLERANCE &&
                WorldTimer::getMSTimeDiff(e.atMs, now) <= KEEP_MS)
                return e.sees ? Sight::Yes : Sight::No;
        }
        if (budget == 0)
            return Sight::Unknown;
        --budget;
        float const half = PositionField::CELL_YD * 0.5f;
        bool const sees = Line(viewer, fx * PositionField::CELL_YD + half, fy * PositionField::CELL_YD + half, from.z,
                               tx * PositionField::CELL_YD + half, ty * PositionField::CELL_YD + half, to.z);
        if (m_entries.size() >= MAX_ENTRIES)
            m_entries.clear();
        Entry& e = m_entries[key];
        e.fromZ = from.z;
        e.toZ = to.z;
        e.atMs = now;
        e.eye = eye;
        e.sees = sees;
        return sees ? Sight::Yes : Sight::No;
    }

    bool SightCache::PathFailed(Unit const* walker, Position3 const& to, bool tight) const
    {
        uint64 const w = walker->GetObjectGuid().GetRawValue();
        int32 const fx = PositionField::CellOf(walker->GetPositionX()), fy = PositionField::CellOf(walker->GetPositionY());
        int32 const tx = PositionField::CellOf(to.x), ty = PositionField::CellOf(to.y);
        uint32 const now = WorldTimer::getMSTime();
        for (PathFail const& f : m_pathFails)
            if (f.walker == w && f.tight == tight && f.fromX == fx && f.fromY == fy && f.toX == tx && f.toY == ty &&
                WorldTimer::getMSTimeDiff(f.atMs, now) <= PATH_FAIL_MS)
                return true;
        return false;
    }

    void SightCache::MarkPathFailed(Unit const* walker, Position3 const& to, bool tight)
    {
        uint32 const now = WorldTimer::getMSTime();
        m_pathFails.erase(std::remove_if(m_pathFails.begin(), m_pathFails.end(),
            [now](PathFail const& f) { return WorldTimer::getMSTimeDiff(f.atMs, now) > PATH_FAIL_MS; }), m_pathFails.end());
        PathFail f;
        f.walker = walker->GetObjectGuid().GetRawValue();
        f.fromX = PositionField::CellOf(walker->GetPositionX());
        f.fromY = PositionField::CellOf(walker->GetPositionY());
        f.toX = PositionField::CellOf(to.x);
        f.toY = PositionField::CellOf(to.y);
        f.tight = tight;
        f.atMs = now;
        m_pathFails.push_back(f);
    }

    bool SightCache::StandFailed(Unit const* viewer, Position3 const& spot, Position3 const& target) const
    {
        uint64 const v = viewer->GetObjectGuid().GetRawValue();
        int32 const ax = PositionField::CellOf(spot.x), ay = PositionField::CellOf(spot.y);
        int32 const tx = PositionField::CellOf(target.x), ty = PositionField::CellOf(target.y);
        uint32 const now = WorldTimer::getMSTime();
        for (StandFail const& f : m_standFails)
            if (f.viewer == v && f.atX == ax && f.atY == ay && f.targetX == tx && f.targetY == ty &&
                WorldTimer::getMSTimeDiff(f.atMs, now) <= STAND_FAIL_MS)
                return true;
        return false;
    }

    void SightCache::MarkStandFailed(Unit const* viewer, Position3 const& target)
    {
        uint32 const now = WorldTimer::getMSTime();
        m_standFails.erase(std::remove_if(m_standFails.begin(), m_standFails.end(),
            [now](StandFail const& f) { return WorldTimer::getMSTimeDiff(f.atMs, now) > STAND_FAIL_MS; }), m_standFails.end());
        StandFail f;
        f.viewer = viewer->GetObjectGuid().GetRawValue();
        f.atX = PositionField::CellOf(viewer->GetPositionX());
        f.atY = PositionField::CellOf(viewer->GetPositionY());
        f.targetX = PositionField::CellOf(target.x);
        f.targetY = PositionField::CellOf(target.y);
        for (StandFail& old : m_standFails)
            if (old.viewer == f.viewer && old.atX == f.atX && old.atY == f.atY && old.targetX == f.targetX && old.targetY == f.targetY)
            {
                old.atMs = now;
                return;
            }
        f.atMs = now;
        m_standFails.push_back(f);
    }
}
