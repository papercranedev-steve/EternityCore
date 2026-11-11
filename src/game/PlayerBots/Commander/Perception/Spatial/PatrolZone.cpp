#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace PatrolZone
{
    namespace
    {
        // The field of this refresh's world (the cache of the rule), null when none.
        PositionField const* FieldOf(WorldModel const& world)
        {
            PositionField const* f = world.field.get();
            return f && f->worldVersion == world.version ? f : nullptr;
        }

        float ArriveAtProbe(PatrolFacts const& p, Position3 const& probe, float ageSec)
        {
            if (!Counts(p))
                return -1.0f;
            float const radius = Radius(p);
            for (size_t i = 0; i < p.track.size(); ++i)
                if (PackResolver::Distance(p.track[i], probe) <= radius)
                    return SecOf(p, i, ageSec);
            return -1.0f;
        }
    }

    bool Counts(PatrolFacts const& p)
    {
        return p.aggressive && !p.noAggro && !p.tappedByOther && p.trackStepSec > 0.0f && !p.track.empty();
    }

    float Radius(PatrolFacts const& p)
    {
        return p.reach + MARGIN;
    }

    float TrackAgeSec(WorldModel const& world)
    {
        return WorldTimer::getMSTimeDiff(world.packsRefreshedAtMs, world.refreshedAtMs) / 1000.0f;
    }

    Position3 Probe(WorldModel const& world, Position3 const& at)
    {
        int32 const cx = PositionField::CellOf(at.x), cy = PositionField::CellOf(at.y);
        Position3 p;
        p.x = (float(cx) + 0.5f) * PositionField::CELL_YD;
        p.y = (float(cy) + 0.5f) * PositionField::CELL_YD;
        p.z = at.z;
        if (PositionField const* f = FieldOf(world))
            if (FieldCell const* c = f->At(cx, cy))
                if (c->standable)
                    p.z = c->z;
        return p;
    }

    float SecOf(PatrolFacts const& p, size_t index, float ageSec)
    {
        return std::max(0.0f, float(index) * p.trackStepSec - ageSec);
    }

    Position3 PositionAt(WorldModel const& world, PatrolFacts const& p, float sec)
    {
        if (p.track.empty())
            return p.pos;
        if (p.trackStepSec <= 0.0f)
            return p.track.front();
        size_t const i = size_t(std::max(0.0f, sec + TrackAgeSec(world)) / p.trackStepSec);
        return p.track[std::min(p.track.size() - 1, i)];
    }

    float ArriveSec(WorldModel const& world, PatrolFacts const& p, Position3 const& at)
    {
        return ArriveAtProbe(p, Probe(world, at), TrackAgeSec(world));
    }

    float ArriveSec(WorldModel const& world, Position3 const& at, PatrolFacts const** which)
    {
        if (which)
            *which = nullptr;
        else if (PositionField const* f = FieldOf(world))
        {
            // The cache: the builder stamped this cell by the same rule.
            FieldCell const* c = f->At(at);
            if (c && c->standable)
                return c->patrolSec < 0.0f ? -1.0f : c->patrolSec;
        }
        Position3 const probe = Probe(world, at);
        float const age = TrackAgeSec(world);
        float best = -1.0f;
        for (PatrolFacts const& p : world.patrols)
        {
            float const t = ArriveAtProbe(p, probe, age);
            if (t >= 0.0f && (best < 0.0f || t < best))
            {
                best = t;
                if (which)
                    *which = &p;
            }
        }
        return best;
    }

    bool Comes(WorldModel const& world, PatrolFacts const& p, Position3 const& at, float withinSec)
    {
        float const t = ArriveSec(world, p, at);
        return t >= 0.0f && t < withinSec;
    }

    bool Comes(WorldModel const& world, Position3 const& at, float withinSec)
    {
        float const t = ArriveSec(world, at);
        return t >= 0.0f && t < withinSec;
    }
}
}
