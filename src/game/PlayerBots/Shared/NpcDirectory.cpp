#include "Shared/NpcDirectory.h"
#include "ObjectMgr.h"
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

namespace BotShared
{
namespace NpcDirectory
{
    namespace
    {
        std::mutex s_lock;
        std::map<uint32, std::shared_ptr<std::vector<ServiceNpc> const>> s_byMap;

        std::shared_ptr<std::vector<ServiceNpc> const> SpawnsOf(uint32 mapId)
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto& slot = s_byMap[mapId];
            if (slot)
                return slot;

            auto spawns = std::make_shared<std::vector<ServiceNpc>>();
            auto collect = [mapId, &spawns](CreatureDataPair const& pair)
            {
                CreatureData const& d = pair.second;
                if (d.position.mapId != mapId || (d.spawn_flags & (SPAWN_FLAG_DISABLED | SPAWN_FLAG_DEAD)) || !d.creature_id[0])
                    return false;
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(d.creature_id[0]);
                if (!info || !info->npc_flags)
                    return false;
                ServiceNpc n;
                n.dbGuid = pair.first;
                n.entry = d.creature_id[0];
                n.npcFlags = info->npc_flags;
                n.x = d.position.x;
                n.y = d.position.y;
                n.z = d.position.z;
                spawns->push_back(n);
                return false;
            };
            sObjectMgr.DoCreatureData(collect);
            slot = spawns;
            return slot;
        }
    }

    std::vector<ServiceNpc> Near(uint32 mapId, float x, float y, float range, uint32 npcFlagMask)
    {
        std::vector<std::pair<float, ServiceNpc>> found;
        float const rangeSq = range * range;
        for (ServiceNpc const& n : *SpawnsOf(mapId))
        {
            if (!(n.npcFlags & npcFlagMask))
                continue;
            float const dx = n.x - x;
            float const dy = n.y - y;
            float const distSq = dx * dx + dy * dy;
            if (distSq <= rangeSq)
                found.emplace_back(distSq, n);
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::vector<ServiceNpc> out;
        out.reserve(found.size());
        for (auto const& f : found)
            out.push_back(f.second);
        return out;
    }

    ObjectGuid GuidOf(ServiceNpc const& npc)
    {
        return ObjectGuid(HIGHGUID_UNIT, npc.entry, npc.dbGuid);
    }
}
}
