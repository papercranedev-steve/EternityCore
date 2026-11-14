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
}
