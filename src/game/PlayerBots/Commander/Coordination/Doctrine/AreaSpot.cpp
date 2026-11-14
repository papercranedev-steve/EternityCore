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

    float SlipAt(WorldModel const& world, PartyMemberFacts const& m, AreaSpellFacts const& spell,
                 Position3 const& centre, Position3 const& p)
    {
        float const channelSec = float(spell.ticks) * spell.periodSec;
        float const gap = Distance2d(p, centre);
        float const sec = AreaValue::WalkSecUnder(centre, AggroTurnCost::RUN_YARDS_PER_SEC, spell.slowPct,
                                                  centre, spell.radius, p, 0.0f);
        float const speed = sec > 0.0f ? gap / sec : AggroTurnCost::RUN_YARDS_PER_SEC;
        return Exposure::ChannelLoss(world, m, gap, speed, channelSec);
    }

    std::vector<CasterCell> CasterCells(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& m,
                                        Position3 const& around, float maxYd)
    {
        std::vector<CasterCell> out;
        if (!world.field)
            return out;
        FieldSpots::Query q;
        q.center = around;
        q.maxYd = maxYd;
        q.patrolBeforeSec = RouteGuard::StaySec(world, plan);
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        for (FieldSpots::Spot const& s : FieldSpots::Collect(world, m, q))
        {
            if (HazardZones::IsInside(zones, HAZARD_RANGED, s.pos))
                continue;
            CasterCell c;
            c.pos = s.pos;
            c.walkCost = SpotCost::Walk(world, m, s.WalkEstimate(PATH_FACTOR) / RUN_YD_PER_SEC);
            c.taken = s.taken;
            out.push_back(c);
        }
        return out;
    }

    Choice Pick(WorldModel const& world, TeamPlan const& plan, AreaValue::Request const& base, bool evaluate,
                Position3 const* prevSpot)
    {
        Choice out;
        PartyMemberFacts const& m = *base.caster;
        AreaSpellFacts const& spell = *base.spell;

        AreaValue::Request here = base;
        here.from = m.pos;
        AreaValue::Result hereValue;
        if (evaluate)
            hereValue = AreaValue::Evaluate(world, here);

        // An enemy at the centre that turns at once (header "slip"); slowed inside the area only
        // (AreaValue::WalkSecUnder). It ranks the cells to value; it is in the score only
        // without enemies to value (before the fight): valued, the worst case is in AreaValue.
        auto const slipAt = [&](Position3 const& p) { return SlipAt(world, m, spell, base.centre, p); };

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
        std::vector<Cell> cells;
        for (CasterCell const& s : CasterCells(world, plan, m, base.centre, spell.range - SPOT_MARGIN_YD))
        {
            Cell c;
            c.pos = s.pos;
            c.walkCost = s.walkCost;
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
