#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <queue>

namespace Commander
{
    int32 PositionField::CellOf(float v)
    {
        return int32(std::floor(v / CELL_YD));
    }

    int32 PositionField::BlockOf(int32 cell)
    {
        // Floor division: cells -1..-8 are block -1.
        return cell >= 0 ? cell / BLOCK_CELLS : -((-cell + BLOCK_CELLS - 1) / BLOCK_CELLS);
    }

    void PositionField::Step(uint8 dir, int32& dx, int32& dy)
    {
        static int32 const DX[FIELD_DIRS] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        static int32 const DY[FIELD_DIRS] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        dx = DX[dir];
        dy = DY[dir];
    }

    int32 PositionField::FlatIndex(int32 cx, int32 cy) const
    {
        int32 const bx = BlockOf(cx), by = BlockOf(cy);
        auto const it = m_blockIndex.find(Key(bx, by));
        if (it == m_blockIndex.end())
            return -1;
        int32 const local = (cy - by * BLOCK_CELLS) * BLOCK_CELLS + (cx - bx * BLOCK_CELLS);
        return int32(it->second) * BLOCK_CELLS * BLOCK_CELLS + local;
    }

    FieldCell const* PositionField::At(int32 cx, int32 cy) const
    {
        int32 const i = FlatIndex(cx, cy);
        if (i < 0)
            return nullptr;
        return &m_blocks[i / (BLOCK_CELLS * BLOCK_CELLS)].cells[i % (BLOCK_CELLS * BLOCK_CELLS)];
    }

    FieldCell* PositionField::MutableAt(int32 cx, int32 cy)
    {
        return const_cast<FieldCell*>(static_cast<PositionField const*>(this)->At(cx, cy));
    }

    Position3 PositionField::CenterOf(int32 cx, int32 cy) const
    {
        Position3 p;
        p.x = (float(cx) + 0.5f) * CELL_YD;
        p.y = (float(cy) + 0.5f) * CELL_YD;
        if (FieldCell const* c = At(cx, cy))
            p.z = c->z;
        return p;
    }

    void PositionField::Clear()
    {
        m_blocks.clear();
        m_blockIndex.clear();
        m_idle.clear();
        m_walks.clear();
        m_views.clear();
    }

    bool PositionField::LineLinked(int32 fx, int32 fy, int32 tx, int32 ty) const
    {
        // Bresenham over cells; every step must follow a link of the cell it leaves.
        int32 const ax = std::abs(tx - fx), ay = std::abs(ty - fy);
        int32 const sx = tx > fx ? 1 : -1, sy = ty > fy ? 1 : -1;
        int32 err = ax - ay;
        int32 x = fx, y = fy;
        while (x != tx || y != ty)
        {
            FieldCell const* c = At(x, y);
            if (!c)
                return false;
            int32 const e2 = 2 * err;
            int32 dx = 0, dy = 0;
            if (e2 > -ay) { err -= ay; dx = sx; }
            if (e2 < ax)  { err += ax; dy = sy; }
            uint8 dir = 0;
            for (uint8 d = 0; d < FIELD_DIRS; ++d)
            {
                int32 ddx, ddy;
                Step(d, ddx, ddy);
                if (ddx == dx && ddy == dy)
                    dir = d;
            }
            if (!(c->links & (1 << dir)))
                return false;
            x += dx;
            y += dy;
        }
        return true;
    }

    bool PositionField::SeesEstimate(Position3 const& from, Position3 const& to) const
    {
        int32 const fx = CellOf(from.x), fy = CellOf(from.y);
        int32 const tx = CellOf(to.x), ty = CellOf(to.y);
        int32 const ti = FlatIndex(tx, ty);
        if (ti < 0 || FlatIndex(fx, fy) < 0)
            return false;
        std::vector<int8>& view = m_views[Key(fx, fy)];
        if (view.empty())
            view.assign(m_blocks.size() * BLOCK_CELLS * BLOCK_CELLS, int8(-1));
        if (view[ti] < 0)
            view[ti] = LineLinked(fx, fy, tx, ty) ? 1 : 0;
        return view[ti] == 1;
    }

    bool PositionField::ClearOfIdle(uint64 mask, Position3 const& at, Position3 const* fightAt, bool tight) const
    {
        if (!mask)
            return true;
        FieldCell const* f = fightAt ? At(*fightAt) : nullptr;
        if (!f)
            return false;
        uint64 const fightMask = tight ? f->idleTight : f->idle;
        if (mask & ~fightMask)
            return false;
        for (uint32 i = 0; mask; ++i, mask >>= 1)
            if ((mask & 1) && SafeRoute::Distance2d(at, m_idle[i].home) <
                                  SafeRoute::Distance2d(*fightAt, m_idle[i].home) - SafeRoute::END_GAP_TOLERANCE)
                return false;
        return true;
    }

    PositionField::Walk const& PositionField::WalkFrom(PartyMemberFacts const& from, bool tight, Position3 const* fightAt) const
    {
        int32 const fightCell = fightAt ? FlatIndex(CellOf(fightAt->x), CellOf(fightAt->y)) : -1;
        for (Walk const& w : m_walks)
            if (w.from == from.guid && w.tight == tight && w.fightCell == fightCell)
                return w;

        m_walks.emplace_back();
        Walk& w = m_walks.back();
        w.from = from.guid;
        w.tight = tight;
        w.fightCell = fightCell;
        auto const idleOf = [tight](FieldCell const& c) { return tight ? c.idleTight : c.idle; };
        w.yd.assign(m_blocks.size() * BLOCK_CELLS * BLOCK_CELLS, -1.0f);

        int32 const sx = CellOf(from.pos.x), sy = CellOf(from.pos.y);
        int32 const start = FlatIndex(sx, sy);
        FieldCell const* startCell = At(sx, sy);
        if (start < 0 || !startCell || !startCell->standable)
            return w;

        // Circles the member stands in: entered only farther from the creature's home than
        // the member is now (SafeRoute::IdleCreatures::LeavingFrom). Circles covering the fight
        // spot: no closer than the fight (ArrivingAt). Both: the smaller gap.
        uint64 const startMask = idleOf(*startCell);
        FieldCell const* fightCellPtr = fightAt ? At(*fightAt) : nullptr;
        uint64 const fightMask = fightCellPtr ? idleOf(*fightCellPtr) : 0;
        uint64 const openMask = startMask | fightMask;
        float gap[MAX_IDLE];
        for (uint32 i = 0; i < m_idle.size(); ++i)
        {
            gap[i] = 1.0e9f;
            if ((startMask >> i) & 1)
                gap[i] = SafeRoute::Distance2d(from.pos, m_idle[i].home);
            if ((fightMask >> i) & 1)
                gap[i] = std::min(gap[i], SafeRoute::Distance2d(*fightAt, m_idle[i].home));
        }
        auto const enterable = [&](FieldCell const& c, Position3 const& at)
        {
            uint64 const mask = idleOf(c);
            if (mask & ~openMask)
                return false;
            uint64 shared = mask & openMask;
            for (uint32 i = 0; shared; ++i, shared >>= 1)
                if ((shared & 1) && SafeRoute::Distance2d(at, m_idle[i].home) < gap[i] - SafeRoute::END_GAP_TOLERANCE)
                    return false;
            return true;
        };

        struct Node
        {
            float yd;
            int32 cx, cy;
            bool operator>(Node const& o) const { return yd > o.yd; }
        };
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        w.yd[start] = 0.0f;
        open.push({ 0.0f, sx, sy });
        while (!open.empty())
        {
            Node const n = open.top();
            open.pop();
            int32 const ni = FlatIndex(n.cx, n.cy);
            if (n.yd > w.yd[ni])
                continue;
            FieldCell const* c = At(n.cx, n.cy);
            for (uint8 d = 0; d < FIELD_DIRS; ++d)
            {
                if (!(c->links & (1 << d)))
                    continue;
                int32 dx, dy;
                Step(d, dx, dy);
                int32 const mx = n.cx + dx, my = n.cy + dy;
                int32 const mi = FlatIndex(mx, my);
                FieldCell const* m = mi >= 0 ? At(mx, my) : nullptr;
                if (!m || !m->standable || !enterable(*m, CenterOf(mx, my)))
                    continue;
                float const dz = m->z - c->z;
                float const yd = n.yd + std::sqrt(StepYd(d) * StepYd(d) + dz * dz);
                if (w.yd[mi] >= 0.0f && w.yd[mi] <= yd)
                    continue;
                w.yd[mi] = yd;
                open.push({ yd, mx, my });
            }
        }
        return w;
    }

    float PositionField::WalkYd(PartyMemberFacts const& from, Position3 const& to, bool tight, Position3 const* fightAt) const
    {
        int32 const i = FlatIndex(CellOf(to.x), CellOf(to.y));
        if (i < 0)
            return -1.0f;
        return WalkFrom(from, tight, fightAt).yd[i];
    }
}
