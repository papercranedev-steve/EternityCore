#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace Commander
{
namespace PackResolver
{
    float Distance(Position3 const& a, Position3 const& b)
    {
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        float const dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    namespace
    {
        struct DisjointSet
        {
            std::vector<size_t> parent;
            std::vector<ObjectGuid> dbLeader;   // per root

            explicit DisjointSet(std::vector<PackCreature> const& creatures)
                : parent(creatures.size()), dbLeader(creatures.size())
            {
                std::iota(parent.begin(), parent.end(), size_t(0));
                for (size_t i = 0; i < creatures.size(); ++i)
                    dbLeader[i] = creatures[i].dbGroupLeader;
            }

            size_t Find(size_t i)
            {
                while (parent[i] != i)
                {
                    parent[i] = parent[parent[i]];
                    i = parent[i];
                }
                return i;
            }

            void Union(size_t a, size_t b)
            {
                size_t ra = Find(a);
                size_t rb = Find(b);
                if (ra == rb)
                    return;
                // never merge two different creature_groups packs
                if (!dbLeader[ra].IsEmpty() && !dbLeader[rb].IsEmpty() && dbLeader[ra] != dbLeader[rb])
                    return;
                if (dbLeader[ra].IsEmpty())
                    dbLeader[ra] = dbLeader[rb];
                parent[rb] = ra;
            }
        };
    }

    std::vector<PackFacts> Resolve(std::vector<PackCreature> const& creatures, Position3 const& leaderPos,
                                   float linkDistance)
    {
        size_t const n = creatures.size();
        DisjointSet sets(creatures);

        // Same creature_groups leader first, so distance links cannot block them.
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j)
                if (!creatures[i].dbGroupLeader.IsEmpty() && creatures[i].dbGroupLeader == creatures[j].dbGroupLeader)
                    sets.Union(i, j);

        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = i + 1; j < n; ++j)
            {
                // distance only links creatures where at least one has no group record
                if (!creatures[i].dbGroupLeader.IsEmpty() && !creatures[j].dbGroupLeader.IsEmpty())
                    continue;
                if (Distance(creatures[i].pos, creatures[j].pos) <= linkDistance)
                    sets.Union(i, j);
            }
        }

        std::map<size_t, PackFacts> byRoot;
        for (size_t i = 0; i < n; ++i)
            byRoot[sets.Find(i)].members.push_back(creatures[i]);

        std::vector<PackFacts> packs;
        packs.reserve(byRoot.size());
        for (auto& itr : byRoot)
        {
            PackFacts& pack = itr.second;
            pack.dbGroupLeader = sets.dbLeader[itr.first];

            bool anyGrouped = false;
            bool anyUngrouped = false;
            for (PackCreature const& c : pack.members)
            {
                pack.center.x += c.pos.x;
                pack.center.y += c.pos.y;
                pack.center.z += c.pos.z;
                if (c.elite)
                    ++pack.eliteCount;
                pack.maxLevel = std::max(pack.maxLevel, c.level);
                if (c.dbGroupLeader.IsEmpty())
                    anyUngrouped = true;
                else
                    anyGrouped = true;
            }
            float const count = float(pack.members.size());
            pack.center.x /= count;
            pack.center.y /= count;
            pack.center.z /= count;
            pack.distanceToLeader = Distance(pack.center, leaderPos);
            pack.source = anyGrouped ? (anyUngrouped ? PackSource::Mixed : PackSource::Database) : PackSource::Distance;
            packs.push_back(std::move(pack));
        }

        std::sort(packs.begin(), packs.end(), [](PackFacts const& a, PackFacts const& b)
        {
            return a.distanceToLeader < b.distanceToLeader;
        });
        return packs;
    }
}
}
