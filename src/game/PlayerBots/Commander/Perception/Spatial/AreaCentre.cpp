#include "Commander/Perception/Spatial/AreaCentre.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace AreaCentre
{
    namespace
    {
        // The smallest t (yards from `from` toward `anchor`) keeping every enemy within its
        // radius - margin. False: none.
        bool Solve(Position3 const& from, float ux, float uy, std::vector<EnemyFacts const*> const& enemies,
                   float radius, float margin, float& t)
        {
            float lo = 0.0f;
            float hi = 1e9f;
            for (EnemyFacts const* e : enemies)
            {
                float const r = radius + e->boundingRadius - margin;
                if (r <= 0.0f)
                    return false;
                float const px = e->pos.x - from.x, py = e->pos.y - from.y;
                float const along = px * ux + py * uy;
                float const perp2 = px * px + py * py - along * along;
                if (perp2 > r * r)
                    return false;
                float const h = std::sqrt(r * r - std::max(0.0f, perp2));
                lo = std::max(lo, along - h);
                hi = std::min(hi, along + h);
            }
            if (lo > hi)
                return false;
            t = lo;
            return true;
        }
    }

    bool NearSide(Position3 const& from, Position3 const& anchor, std::vector<EnemyFacts const*> const& enemies,
                  float radius, Position3& out)
    {
        if (enemies.empty())
            return false;
        float const dx = anchor.x - from.x, dy = anchor.y - from.y;
        float const length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.1f)
            return false;
        float const ux = dx / length, uy = dy / length;
        float t = 0.0f;
        if (!Solve(from, ux, uy, enemies, radius, MARGIN_YD, t) && !Solve(from, ux, uy, enemies, radius, 0.0f, t))
            return false;
        out = anchor;
        out.x = from.x + ux * t;
        out.y = from.y + uy * t;
        return true;
    }

    Position3 AheadOf(EnemyFacts const& lead, Position3 const& to, float radius)
    {
        float const dx = to.x - lead.pos.x, dy = to.y - lead.pos.y;
        float const length = std::sqrt(dx * dx + dy * dy);
        Position3 out = lead.pos;
        if (length < 0.1f)
            return out;
        float const k = std::min(length, std::max(0.0f, radius + lead.boundingRadius - MARGIN_YD));
        out.x += dx * k / length;
        out.y += dy * k / length;
        out.z += (to.z - lead.pos.z) * k / length;
        return out;
    }
}
}
