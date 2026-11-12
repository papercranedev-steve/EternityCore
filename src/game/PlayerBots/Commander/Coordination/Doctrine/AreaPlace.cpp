#include "Commander/Coordination/Doctrine/AreaPlace.h"
#include "Commander/Coordination/Doctrine/AreaSpot.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Spatial/AreaCentre.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace AreaPlace
{
    Choice OnPile(WorldModel const& world, TeamPlan const& plan, AreaValue::Request const& base,
                  EnemyFacts const& anchor, Position3 const* prevSpot)
    {
        Choice out;
        PartyMemberFacts const& m = *base.caster;
        AreaSpellFacts const& spell = *base.spell;
        AreaValue::Request r = base;
        r.centre = anchor.pos;
        r.from = m.pos;
        AreaSpot::Choice const ch = AreaSpot::Pick(world, plan, r, true, prevSpot);
        if (!ch.valid)
            return out;
        out.valid = true;
        out.netHere = ch.hereValid ? ch.hereNet : 0.0f;
        out.value = ch.value;
        out.move = ch.move;
        out.spots = ch.spots;
        out.walkCost = ch.walkCost;
        out.gain = ch.gain;
        if (ch.move)
            r.from = ch.spots.front();
        out.request = r;

        // The near side of what it covers, from that spot.
        std::vector<EnemyFacts const*> covered;
        for (ObjectGuid g : ch.value.targets)
            if (EnemyFacts const* e = world.FindEnemy(g))
                covered.push_back(e);
        Position3 side;
        if (!AreaCentre::NearSide(r.from, anchor.pos, covered, spell.radius, side))
            return out;
        AreaValue::Request moved = r;
        moved.centre = side;
        AreaValue::Result const v = AreaValue::Evaluate(world, moved);
        if (!v.valid || v.net <= out.value.net)
            return out;
        out.request = moved;
        out.value = v;
        // Here: the moved centre is nearer the spot, not always nearer the caster.
        if (PackResolver::Distance(m.pos, side) > spell.range)
            out.netHere = 0.0f;
        if (out.move)
        {
            std::vector<Position3> spots;
            for (Position3 const& s : out.spots)
                if (PackResolver::Distance(s, side) <= spell.range)
                    spots.push_back(s);
            out.spots = spots;
        }
        return out;
    }

    Choice OnLead(WorldModel const& world, AreaValue::Request const& base, EnemyFacts const& lead)
    {
        Choice out;
        PartyMemberFacts const& m = *base.caster;
        AreaSpellFacts const& spell = *base.spell;
        AreaValue::Request r = base;
        r.from = m.pos;
        r.centre = AreaCentre::AheadOf(lead, m.pos, spell.radius);
        if (PackResolver::Distance(m.pos, r.centre) > spell.range || spell.periodSec <= 0.0f)
            return out;
        AreaValue::Result const v = AreaValue::Evaluate(world, r);
        if (!v.valid)
            return out;

        // It pays before the lead is on the caster.
        float const arriveSec = Runners::ArriveSec(lead, m.pos, &spell, r.centre) - AreaCentre::CAST_DELAY_SEC;
        uint32 const before = arriveSec > 0.0f ? uint32(arriveSec / spell.periodSec) : 0;
        if (!before)
            return out;
        if (before < spell.ticks)
        {
            AreaValue::Request q = r;
            q.ticks = before;
            AreaValue::Result const w = AreaValue::Evaluate(world, q);
            if (!w.valid || w.net <= 0.0f)
                return out;
        }
        out.valid = true;
        out.request = r;
        out.value = v;
        out.netHere = v.net;
        return out;
    }
}
}
