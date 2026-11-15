#include "Commander/Behaviors/Movement/RangedSpot.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SightCache.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SightMargin.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Player.h"
#include <algorithm>
#include <memory>

namespace Commander
{
namespace RangedSpot
{
    namespace
    {
        using SafeRoute::Distance2d;

        // Cells estimated in sight first (header), then the loss.
        float Key(Spot const& s)
        {
            return s.loss + (s.viewed ? 0.0f : FieldSpots::HIDDEN_KEY);
        }

        // Cells the grid walk reaches first, then the others; each part by Key, PreferFree
        // within a part only.
        void SortSpots(WorldModel const& world, PartyMemberFacts const& self, float speed, std::vector<Spot>& spots)
        {
            auto const firstUnreached = std::stable_partition(spots.begin(), spots.end(), [](Spot const& s) { return s.reached; });
            size_t const reachedCount = size_t(firstUnreached - spots.begin());
            // Equally good: within one cell of walk.
            float const tie = SpotCost::Walk(world, self, PositionField::CELL_YD * PATH_FACTOR / speed);
            std::vector<Spot> rest(spots.begin() + reachedCount, spots.end());
            spots.resize(reachedCount);
            for (std::vector<Spot>* part : { &spots, &rest })
            {
                std::stable_sort(part->begin(), part->end(), [](Spot const& a, Spot const& b) { return Key(a) < Key(b); });
                FieldSpots::PreferFree(*part, Key, [](Spot const& s) { return s.taken; }, tie);
            }
            spots.insert(spots.end(), rest.begin(), rest.end());
        }

        // The band's cells with their loss, best first (header "cells", "loss per cell").
        std::vector<Spot> Candidates(WorldModel const& world, PartyMemberFacts const& self,
                                     std::vector<HazardZone> const& zones, Query const& q, float speed, bool tight)
        {
            static std::vector<EnemyFacts const*> const noTurnable;
            static std::vector<Position3> const noPoints;
            std::vector<EnemyFacts const*> const& turnable = q.turnable ? *q.turnable : noTurnable;
            std::vector<Position3> const& points = q.points ? *q.points : noPoints;
            PositionField const& field = *world.field;
            std::vector<Spot> spots;
            FieldSpots::Query fq;
            fq.center = q.at;
            fq.minYd = q.minYd;
            fq.maxYd = q.maxYd;
            fq.patrolBeforeSec = q.staySec;
            fq.tight = tight;
            // In the fight: circles covering `at` as the fight there (FieldSpots.h).
            fq.hasFight = !q.prepare;
            fq.fightAt = q.at;
            // In the fight also the cells the grid walk misses: their path check is the only way
            // to a blocker for the join gamble (FieldSpots::Query::keepUnreached).
            fq.keepUnreached = !q.prepare;
            for (FieldSpots::Spot const& c : FieldSpots::Collect(world, self, fq))
            {
                if (HazardZones::IsInside(zones, HAZARD_RANGED, c.pos))
                    continue;
                Spot s;
                s.pos = c.pos;
                s.walkSec = c.WalkEstimate(PATH_FACTOR) / speed;
                if (q.keep && !q.keep(s))
                    continue;
                s.exposure = Exposure::LossAt(world, self, turnable, c.pos);
                s.sight = SightMargin::LossAt(world, self, field, points, c.pos);
                s.loss = SpotCost::Walk(world, self, s.walkSec) + s.exposure + s.sight;
                s.viewed = field.SeesEstimate(q.at, c.pos);
                s.taken = c.taken;
                s.reached = c.reached;
                spots.push_back(s);
            }
            SortSpots(world, self, speed, spots);
            return spots;
        }
    }

    bool Find(Player* me, WorldModel const& world, PartyMemberFacts const& self,
              SafeRoute::IdleCreatures const& idle, Query const& q, uint32& budget, Result& out)
    {
        if (!world.field || !world.field->sight)
            return false;
        SightCache& sight = *world.field->sight;
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();

        std::unique_ptr<SafeRoute::IdleCreatures> idleTight;
        for (bool const tight : { false, true })
        {
            if (tight && !q.mayTighten)
                break;
            std::vector<Spot> spots = Candidates(world, self, zones, q, speed, tight);
            out.cells += spots.size();
            // This bot's own idle scan on the pass's line (scanned again only for the hard line).
            if (tight)
                idleTight.reset(new SafeRoute::IdleCreatures(me, nullptr, SafeRoute::TIGHT_MARGIN));
            SafeRoute::IdleCreatures const& own = tight ? *idleTight : idle;
            // The walk out of an idle creature's reach starts inside it. In the fight, a circle
            // that covers `at` counts only closer to its home than `at` (MeleeRoutes.h, the
            // melee's walk in; stockade-20261115-002203: every walk into the tank's cell failed
            // on such a circle).
            SafeRoute::IdleCreatures pathIdle = q.unsafe ? own.LeavingFrom(here) : own;
            if (!q.prepare)
                pathIdle = pathIdle.ArrivingAt(q.at);
            // The spot itself by the same rule (stockade-20261115-012103, FieldSpots.h).
            SafeRoute::IdleCreatures const endIdle = q.prepare ? own : own.ArrivingAt(q.at);
            // One pass in order (header). (Before 2026-11-15 the cells were probed nearest first
            // with the real line only: the cells behind a wall used the budget,
            // stockade-20261115-002203.)
            uint32 checks = 0;
            for (Spot const& s : spots)
            {
                if (q.hereValid && q.lossHere - s.loss <= q.margin)
                {
                    if (s.viewed)
                        continue;   // (further down an estimate in sight may still pay)
                    break;
                }
                if (endIdle.Near(s.pos))
                {
                    ++out.why.idle;
                    continue;
                }
                if (sight.PathFailed(me, s.pos, tight))
                {
                    ++out.why.pathKept;
                    continue;
                }
                ++out.checked;
                SightCache::Sight const v = sight.Cells(me, s.pos, q.at, budget);
                if (v == SightCache::Sight::Unknown)
                {
                    out.why.budget = true;
                    break;      // out of budget: the next ticks go on (the answers are kept)
                }
                out.why.Real(s.viewed, v == SightCache::Sight::Yes);
                if (v == SightCache::Sight::No)
                {
                    ++out.why.hidden;
                    continue;
                }
                ++out.why.seen;
                if (checks >= MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                // The kept answer is from the cells' centres: the spell's line now, then the path.
                if (!SightCache::Live(me, s.pos, q.at))
                {
                    ++out.why.live;
                    continue;
                }
                SafeRoute::Verdict verdict;
                if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MAX_PATH_DETOUR, nullptr, nullptr, &verdict))
                {
                    out.why.Path(verdict, s.pos);
                    sight.MarkPathFailed(me, s.pos, tight);
                    continue;
                }
                out.spot = s;
                out.tight = tight;
                return true;
            }
        }
        return false;
    }
}
}
