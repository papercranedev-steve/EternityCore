#include "Commander/Coordination/LosBreak.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/CreatureProfile.h"
#include "Creature.h"
#include "Map.h"
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
            };
        }

        bool FindPoint(Map* map, Search const& search, Position3& point)
        {
            if (search.casters.empty() || !search.mover)
                return false;

            Position3 const& center = search.center;
            Creature const* first = search.casters.front();
            float const away = std::atan2(center.y - first->GetPositionY(), center.x - first->GetPositionX());

            // 1. Per ray, the nearest point no caster sees.
            uint32 losChecks = 0;
            std::vector<Candidate> candidates;
            for (uint32 k = 0; k < DIRECTIONS && losChecks < MAX_LOS_CHECKS; ++k)
            {
                // 0, +1, -1, +2, -2 ... steps away from straight away from the first caster.
                int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                float const angle = away + 2.0f * float(M_PI) * float(step) / float(DIRECTIONS);
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
                    candidates.push_back(cand);
                    break;
                }
            }
            if (candidates.empty())
                return false;

            // 2. Nearest first: watchers see it and the mover can walk there.
            std::stable_sort(candidates.begin(), candidates.end(),
                [](Candidate const& a, Candidate const& b) { return a.distance < b.distance; });
            Player* walker = search.mover->ToPlayer();
            // Scanned once, only when there is a candidate (a grid visit).
            std::unique_ptr<SafeRoute::IdleCreatures const> idle;
            if (walker)
                idle.reset(new SafeRoute::IdleCreatures(walker));

            uint32 paths = 0;
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
                ++paths;
                bool const reachable = walker
                    ? SafeRoute::PathIsSafe(walker, *idle, p, MAX_DETOUR)
                    : search.mover->IsWithinLOS(p.x, p.y, p.z, true, UNIT_HEIGHT);
                if (reachable)
                {
                    point = p;
                    return true;
                }
            }
            return false;
        }

        bool IsRangedCaster(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile && profile->mainRangedSpellId;
        }
    }
}
