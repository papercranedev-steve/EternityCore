#include "Commander/Coordination/Doctrine/AreaSpot.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Values/AggroTurnCost.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SpotCost.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace AreaSpot
{
    namespace
    {
        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        struct Cell
        {
            Position3 pos;
            float walkCost = 0.0f;
            float slip = 0.0f;
            bool taken = false;
            AreaValue::Result value;
            float score = 0.0f;
        };
    }

    Choice Pick(WorldModel const& world, TeamPlan const& plan, AreaValue::Request const& base, bool evaluate,
                Position3 const* prevSpot)
    {
        Choice out;
        PartyMemberFacts const& m = *base.caster;
        AreaSpellFacts const& spell = *base.spell;
        float const channelSec = float(spell.ticks) * spell.periodSec;

        AreaValue::Request here = base;
        here.from = m.pos;
        AreaValue::Result hereValue;
        if (evaluate)
            hereValue = AreaValue::Evaluate(world, here);

        // An enemy at the centre that turns at once (header "slip"); slowed inside the area only
        // (AreaValue::WalkSecUnder). It ranks the cells to value; it is in the score only
        // without enemies to value (before the fight): valued, the worst case is in AreaValue.
        auto const slipAt = [&](Position3 const& p)
        {
            float const gap = Distance2d(p, base.centre);
            float const sec = AreaValue::WalkSecUnder(base.centre, AggroTurnCost::RUN_YARDS_PER_SEC, spell.slowPct,
                                                      base.centre, spell.radius, p, 0.0f);
            float const speed = sec > 0.0f ? gap / sec : AggroTurnCost::RUN_YARDS_PER_SEC;
            return Exposure::ChannelLoss(world, m, gap, speed, channelSec);
        };

        // Here.
        bool const idleHere = world.field && world.field->At(m.pos) && world.field->At(m.pos)->idle;
        out.hereValid = PackResolver::Distance(m.pos, base.centre) <= spell.range && !idleHere &&
                        (!evaluate || hereValue.valid);
        out.hereNet = hereValue.valid ? hereValue.net : 0.0f;
        float const hereScore = out.hereNet - (evaluate ? 0.0f : slipAt(m.pos));
        out.value = hereValue;
        out.valid = out.hereValid;

        if (!world.field)
            return out;

        // Cells.
        FieldSpots::Query q;
        q.center = base.centre;
        q.maxYd = spell.range - SPOT_MARGIN_YD;
        q.patrolBeforeSec = RouteGuard::StaySec(world, plan);
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        std::vector<Cell> cells;
        for (FieldSpots::Spot const& s : FieldSpots::Collect(world, m, q))
        {
            if (HazardZones::IsInside(zones, HAZARD_RANGED, s.pos))
                continue;
            Cell c;
            c.pos = s.pos;
            c.walkCost = SpotCost::Walk(world, m, s.WalkEstimate(PATH_FACTOR) / RUN_YD_PER_SEC);
            c.slip = slipAt(s.pos);
            c.taken = s.taken;
            cells.push_back(c);
        }
        if (cells.empty())
            return out;
        float const margin = SpotCost::Walk(world, m, MOVE_MARGIN_SEC);
        auto const sticky = [&](Cell const& c)
        {
            return prevSpot && Distance2d(c.pos, *prevSpot) <= PositionField::CELL_YD ? margin : 0.0f;
        };
        auto const cheap = [&](Cell const& c) { return c.slip + c.walkCost - sticky(c); };
        std::sort(cells.begin(), cells.end(), [&](Cell const& a, Cell const& b) { return cheap(a) < cheap(b); });
        float const tie = SpotCost::Walk(world, m, PositionField::CELL_YD * PATH_FACTOR / RUN_YD_PER_SEC);
        FieldSpots::PreferFree(cells, cheap, [](Cell const& c) { return c.taken; }, tie);

        // The best few, valued from there.
        std::vector<Cell> valued;
        for (Cell& c : cells)
        {
            if (valued.size() >= MAX_EVALUATED)
                break;
            if (evaluate)
            {
                AreaValue::Request r = base;
                r.from = c.pos;
                c.value = AreaValue::Evaluate(world, r);
                if (!c.value.valid)
                    continue;
            }
            c.score = (evaluate ? c.value.net : -c.slip) - c.walkCost + sticky(c);
            valued.push_back(c);
        }
        if (valued.empty())
            return out;
        std::stable_sort(valued.begin(), valued.end(), [](Cell const& a, Cell const& b) { return a.score > b.score; });

        Cell const& best = valued.front();
        if (out.hereValid && best.score - hereScore <= margin)
            return out;
        out.valid = true;
        out.move = true;
        out.value = best.value;
        out.walkCost = best.walkCost;
        out.gain = out.hereValid ? best.score - hereScore : best.score;
        for (Cell const& c : valued)
            if (out.spots.size() < MAX_SPOTS)
                out.spots.push_back(c.pos);
        return out;
    }
}
}
