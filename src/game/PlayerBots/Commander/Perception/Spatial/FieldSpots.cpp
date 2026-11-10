#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"

namespace Commander
{
namespace FieldSpots
{
    std::vector<Spot> Collect(PositionField const& field, PartyMemberFacts const& member, Query const& q)
    {
        std::vector<Spot> out;
        FieldCell const* own = field.At(member.pos);
        bool const walks = own && own->standable;

        int32 const cx0 = PositionField::CellOf(q.center.x - q.maxYd), cx1 = PositionField::CellOf(q.center.x + q.maxYd);
        int32 const cy0 = PositionField::CellOf(q.center.y - q.maxYd), cy1 = PositionField::CellOf(q.center.y + q.maxYd);
        for (int32 cx = cx0; cx <= cx1; ++cx)
        {
            for (int32 cy = cy0; cy <= cy1; ++cy)
            {
                FieldCell const* c = field.At(cx, cy);
                if (!c || !c->standable || c->idle)
                    continue;
                if (q.patrolBeforeSec > 0.0f && c->patrolSec >= 0.0f && c->patrolSec < q.patrolBeforeSec)
                    continue;
                Spot s;
                s.pos = field.CenterOf(cx, cy);
                float const d = SafeRoute::Distance2d(s.pos, q.center);
                if (d < q.minYd || d > q.maxYd)
                    continue;
                s.straightYd = SafeRoute::Distance2d(s.pos, member.pos);
                s.patrolSec = c->patrolSec;
                if (walks)
                {
                    s.walkYd = field.WalkYd(member, s.pos);
                    if (s.walkYd < 0.0f)
                        continue;
                }
                out.push_back(s);
            }
        }
        return out;
    }
}
}
