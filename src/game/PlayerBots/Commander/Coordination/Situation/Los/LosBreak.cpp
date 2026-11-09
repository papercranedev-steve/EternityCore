#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Creature.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace Commander
{
    namespace LosBreak
    {
        namespace
        {
            float const UNIT_HEIGHT = 2.0f;     // WorldObject::IsWithinLOS default target height

            struct Candidate
            {
                Position3 pos;
                float distance = 0.0f;
                bool flat = true;               // FLAT_PROBE_YD around it within FLAT_TOLERANCE
            };

            float Distance2d(Position3 const& a, Position3 const& b)
            {
                float const dx = a.x - b.x, dy = a.y - b.y;
                return std::sqrt(dx * dx + dy * dy);
            }

            // The ground FLAT_PROBE_YD around `p` (4 directions) is within FLAT_TOLERANCE of its
            // height: not the top of a crate or a rubble pile the ray happened to hit
            // (strat-20261108-133706: 141.2 on a crate, 138.5 one yard either side).
            bool Flat(Map* map, Position3 const& p)
            {
                static float const dirs[4][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
                for (auto const& d : dirs)
                {
                    float const x = p.x + d[0] * FLAT_PROBE_YD, y = p.y + d[1] * FLAT_PROBE_YD;
                    float const ground = map->GetHeight(x, y, p.z + 2.0f);
                    if (ground <= INVALID_HEIGHT || std::fabs(ground - p.z) > FLAT_TOLERANCE)
                        return false;
                }
                return true;
            }

            // Farthest a vertex of the caster's navmesh path to `p` lies off the straight line
            // caster -> p (ground plane). 0 without a navmesh path.
            float PathBend(Creature const* caster, Position3 const& p)
            {
                PathInfo path(caster);
                path.calculate(p.x, p.y, p.z);
                if (path.getPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH))
                    return 0.0f;
                Position3 a;
                a.x = caster->GetPositionX();
                a.y = caster->GetPositionY();
                float const dx = p.x - a.x, dy = p.y - a.y;
                float const len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.1f)
                    return 0.0f;
                float bend = 0.0f;
                for (auto const& v : path.getPath())
                    bend = std::max(bend, std::fabs((v.x - a.x) * dy - (v.y - a.y) * dx) / len);
                return bend;
            }

            // Lower is better (LosBreak.h, "ranking").
            uint32 Rank(PointQuality const& q)
            {
                return (q.groundOk ? 0 : 2) + (q.corner ? 0 : 1);
            }
        }

        bool FindPoint(Map* map, Search const& search, Position3& point, PointQuality* quality)
        {
            if (search.casters.empty() || !search.mover)
                return false;
            Position3 const& center = search.center;
            Creature const* first = search.casters.front();
            float const away = std::atan2(center.y - first->GetPositionY(), center.x - first->GetPositionX());

            // 1. Per ray, the nearest point no caster sees; if that one is not flat, the nearest
            // flat one further out as well (the uneven one stays as a fallback).
            uint32 losChecks = 0;
            std::vector<Candidate> candidates;
            for (uint32 k = 0; k < DIRECTIONS && losChecks < MAX_LOS_CHECKS; ++k)
            {
                // 0, +1, -1, +2, -2 ... steps away from straight away from the first caster.
                int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                float const angle = away + 2.0f * float(M_PI) * float(step) / float(DIRECTIONS);
                bool haveUneven = false;
                for (float d = FIRST_STEP; d <= MAX_DISTANCE + 0.01f && losChecks < MAX_LOS_CHECKS; d += STEP)
                {
                    Position3 p;
                    p.x = center.x + std::cos(angle) * d;
                    p.y = center.y + std::sin(angle) * d;
                    // Ground near the center's height: no ledges, no floors above or below.
                    float const ground = map->GetHeight(p.x, p.y, center.z + 2.0f);
                    if (ground <= INVALID_HEIGHT || std::fabs(ground - center.z) > MAX_HEIGHT_DIFF)
                        continue;
                    p.z = ground;
                    if (search.rejects && search.rejects(p))
                        continue;
                    // Spell line of sight is checked from the caster's eyes to the target's
                    // position + 1 yd (WorldObject::IsWithinLOSInMap, GetLosCheckPosition): same here.
                    bool seen = false;
                    for (Creature const* c : search.casters)
                    {
                        ++losChecks;
                        if (c->IsWithinLOS(p.x, p.y, p.z, true, 1.0f))
                        {
                            seen = true;
                            break;
                        }
                    }
                    if (seen)
                        continue;
                    Candidate cand;
                    cand.pos = p;
                    cand.distance = d;
                    cand.flat = Flat(map, p);
                    if (!cand.flat && haveUneven)
                        continue;
                    candidates.push_back(cand);
                    if (cand.flat)
                        break;
                    haveUneven = true;
                }
            }
            if (candidates.empty())
                return false;

            // 2. Flat ones first, nearest first: watchers see it and the mover can walk there.
            std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
            {
                if (a.flat != b.flat)
                    return a.flat;
                return a.distance < b.distance;
            });
            Player* walker = search.mover->ToPlayer();
            // Scanned once, only when there is a candidate (a grid visit).
            std::unique_ptr<SafeRoute::IdleCreatures const> idle;
            if (walker)
                idle.reset(new SafeRoute::IdleCreatures(walker));
            uint32 paths = 0;
            bool found = false;
            PointQuality best;
            for (Candidate const& cand : candidates)
            {
                if (paths >= MAX_PATHS)
                    break;
                Position3 const& p = cand.pos;
                bool watched = true;
                for (Unit const* w : search.watchers)
                {
                    if (!w->IsWithinLOS(p.x, p.y, p.z, true, UNIT_HEIGHT))
                    {
                        watched = false;
                        break;
                    }
                }
                if (!watched)
                    continue;
                bool const seesAll = std::all_of(search.sees.begin(), search.sees.end(), [map, &p](Position3 const& s)
                {
                    return map->isInLineOfSight(p.x, p.y, p.z + UNIT_HEIGHT, s.x, s.y, s.z + UNIT_HEIGHT);
                });
                if (!seesAll)
                    continue;
                ++paths;
                Position3 end = p;
                bool const reachable = walker
                    ? SafeRoute::PathIsSafe(walker, *idle, p, MAX_DETOUR, nullptr, &end)
                    : search.mover->IsWithinLOS(p.x, p.y, p.z, true, UNIT_HEIGHT);
                if (!reachable || (search.accepts && !search.accepts(p)))
                    continue;

                PointQuality q;
                q.groundOk = cand.flat && std::fabs(end.z - p.z) <= MESH_Z_TOLERANCE &&
                             Distance2d(end, p) <= SafeRoute::PATH_END_TOLERANCE;
                q.corner = std::all_of(search.casters.begin(), search.casters.end(),
                    [&p](Creature const* c) { return PathBend(c, p) >= CORNER_MIN_BEND_YD; });
                // Nearest of the best rank (candidates are nearest first within flat / uneven).
                if (!found || Rank(q) < Rank(best))
                {
                    found = true;
                    best = q;
                    point = p;
                    if (Rank(q) == 0)
                        break;
                }
            }
            if (found && quality)
                *quality = best;
            return found;
        }

        bool IsRangedCaster(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile && profile->mainRangedSpellId;
        }
    }
}
