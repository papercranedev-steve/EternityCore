#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"

namespace Commander
{
namespace FieldSpots
{
    std::vector<Spot> Collect(WorldModel const& world, PartyMemberFacts const& member, Query const& q)
    {
        PositionField const& field = *world.field;
        std::vector<Position3> held;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive && m.guid != member.guid)
                held.push_back(m.standAt);
        std::vector<Spot> out;
        std::vector<Spot> unreached;    // cells the grid walk does not get to
        FieldCell const* own = field.At(member.pos);
        bool const walks = own && own->standable;
        Position3 const* fight = q.hasFight ? &q.fightAt : nullptr;

        int32 const cx0 = PositionField::CellOf(q.center.x - q.maxYd), cx1 = PositionField::CellOf(q.center.x + q.maxYd);
        int32 const cy0 = PositionField::CellOf(q.center.y - q.maxYd), cy1 = PositionField::CellOf(q.center.y + q.maxYd);
        for (int32 cx = cx0; cx <= cx1; ++cx)
        {
            for (int32 cy = cy0; cy <= cy1; ++cy)
            {
                FieldCell const* c = field.At(cx, cy);
                if (!c || !c->standable)
                    continue;
                if (q.patrolBeforeSec > 0.0f && c->patrolSec >= 0.0f && c->patrolSec < q.patrolBeforeSec)
                    continue;
                Spot s;
                s.pos = field.CenterOf(cx, cy);
                if (!field.ClearOfIdle(q.tight ? c->idleTight : c->idle, s.pos, fight, q.tight))
                    continue;
                float const d = SafeRoute::Distance2d(s.pos, q.center);
                if (d < q.minYd || d > q.maxYd)
                    continue;
                s.straightYd = SafeRoute::Distance2d(s.pos, member.pos);
                s.patrolSec = c->patrolSec;
                for (Position3 const& h : held)
                    if (SafeRoute::Distance2d(s.pos, h) <= TAKEN_YD)
                        s.taken = true;
                if (walks)
                {
                    s.walkYd = field.WalkYd(member, s.pos, q.tight, fight);
                    if (s.walkYd < 0.0f)
                    {
                        s.reached = false;
                        unreached.push_back(s);
                        continue;
                    }
                }
                out.push_back(s);
            }
        }
        // The walk reaches none of the band (the member is far off, past the field's covered
        // part): all of them, the caller's path check decides.
        if (out.empty())
            out.swap(unreached);
        else if (q.keepUnreached)
            out.insert(out.end(), unreached.begin(), unreached.end());
        return out;
    }
}
}
