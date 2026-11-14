#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include <cmath>
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
    }

    PositionField::Walk const& PositionField::WalkFrom(PartyMemberFacts const& from, bool tight) const
    {
        for (Walk const& w : m_walks)
            if (w.from == from.guid && w.tight == tight)
                return w;

        m_walks.emplace_back();
        Walk& w = m_walks.back();
        w.from = from.guid;
        w.tight = tight;
        auto const idleOf = [tight](FieldCell const& c) { return tight ? c.idleTight : c.idle; };
        w.yd.assign(m_blocks.size() * BLOCK_CELLS * BLOCK_CELLS, -1.0f);

        int32 const sx = CellOf(from.pos.x), sy = CellOf(from.pos.y);
        int32 const start = FlatIndex(sx, sy);
        FieldCell const* startCell = At(sx, sy);
        if (start < 0 || !startCell || !startCell->standable)
            return w;

        // Circles the member stands in: entered only farther from the creature's home than
        // the member is now (SafeRoute::IdleCreatures::LeavingFrom).
        uint64 const startMask = idleOf(*startCell);
        float startGap[MAX_IDLE];
        for (uint32 i = 0; i < m_idle.size(); ++i)
            startGap[i] = (startMask >> i) & 1 ? SafeRoute::Distance2d(from.pos, m_idle[i].home) : 0.0f;
        auto const enterable = [&](FieldCell const& c, Position3 const& at)
        {
            uint64 const mask = idleOf(c);
            if (mask & ~startMask)
                return false;
            uint64 shared = mask & startMask;
            for (uint32 i = 0; shared; ++i, shared >>= 1)
                if ((shared & 1) && SafeRoute::Distance2d(at, m_idle[i].home) < startGap[i] - SafeRoute::END_GAP_TOLERANCE)
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

    float PositionField::WalkYd(PartyMemberFacts const& from, Position3 const& to, bool tight) const
    {
        int32 const i = FlatIndex(CellOf(to.x), CellOf(to.y));
        if (i < 0)
            return -1.0f;
        return WalkFrom(from, tight).yd[i];
    }
}
