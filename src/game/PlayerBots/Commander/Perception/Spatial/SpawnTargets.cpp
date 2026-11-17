#include "Commander/Perception/Spatial/SpawnTargets.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Map.h"
#include "MapPersistentStateMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include <ctime>
#include <map>
#include <memory>
#include <mutex>

namespace Commander
{
namespace SpawnTargets
{
    namespace
    {
        struct Spawn
        {
            uint32 dbGuid;
            uint32 entry;       // creature_id[0]: the guid's entry
            Position3 pos;
        };

        std::mutex s_lock;
        std::map<uint32, std::shared_ptr<std::vector<Spawn> const>> s_byMap;

        std::shared_ptr<std::vector<Spawn> const> SpawnsOf(uint32 mapId)
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto& slot = s_byMap[mapId];
            if (slot)
                return slot;

            auto spawns = std::make_shared<std::vector<Spawn>>();
            auto collect = [mapId, &spawns](CreatureDataPair const& pair)
            {
                CreatureData const& d = pair.second;
                if (d.position.mapId != mapId || (d.spawn_flags & (SPAWN_FLAG_DISABLED | SPAWN_FLAG_DEAD)) ||
                    !d.health_percent || !d.creature_id[0])
                    return false;
                Spawn s;
                s.dbGuid = pair.first;
                s.entry = d.creature_id[0];
                s.pos.x = d.position.x;
                s.pos.y = d.position.y;
                s.pos.z = d.position.z;
                spawns->push_back(s);
                return false;
            };
            sObjectMgr.DoCreatureData(collect);
            slot = spawns;
            return slot;
        }

        bool IsBossEntry(uint32 entry, CreatureInfo const* info)
        {
            if (info && info->rank == CREATURE_ELITE_WORLDBOSS)
                return true;
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile && profile->boss;
        }

        // The same filter as the pack scan (WorldModelBuilder::RefreshPacks), on a live creature.
        bool IsTarget(Creature* c, Player const* viewer)
        {
            if (!c->IsAlive() || c->IsInCombat() || c->GetOwnerGuid() || c->IsPet() || c->IsTotem() ||
                c->IsTemporarySummon() || c->GetCreatureType() == CREATURE_TYPE_CRITTER)
                return false;
            if (c->IsGuard() || c->IsCivilian() || c->IsPvP() || c->IsContestedGuard())
                return false;
            if (FactionTemplateEntry const* faction = c->GetFactionTemplateEntry())
                if (faction->ourMask & (FACTION_MASK_ALLIANCE | FACTION_MASK_HORDE))
                    return false;
            return viewer->IsValidAttackTarget(c) && viewer->IsHostileTo(c);
        }

        // A spawn whose grid is not loaded: from its template.
        bool IsTargetTemplate(CreatureInfo const* info, Player const* viewer)
        {
            if (!info || info->type == CREATURE_TYPE_CRITTER || info->type == CREATURE_TYPE_TOTEM)
                return false;
            FactionTemplateEntry const* faction = sObjectMgr.GetFactionTemplateEntry(info->faction);
            FactionTemplateEntry const* mine = viewer->GetFactionTemplateEntry();
            if (!faction || !mine || (faction->ourMask & (FACTION_MASK_ALLIANCE | FACTION_MASK_HORDE)))
                return false;
            return faction->IsHostileTo(*mine);
        }
    }

    std::vector<Target> Alive(Map* map, Player const* viewer)
    {
        std::vector<Target> out;
        if (!map || !viewer)
            return out;
        std::shared_ptr<std::vector<Spawn> const> const spawns = SpawnsOf(map->GetId());
        MapPersistentState const* state = map->GetPersistentState();
        time_t const now = time(nullptr);

        for (Spawn const& s : *spawns)
        {
            Target t;
            t.dbGuid = s.dbGuid;
            t.entry = s.entry;
            t.guid = ObjectGuid(HIGHGUID_UNIT, s.entry, s.dbGuid);
            if (Creature* c = map->GetCreature(t.guid))
            {
                if (!IsTarget(c, viewer))
                    continue;
                t.pos.x = c->GetPositionX();
                t.pos.y = c->GetPositionY();
                t.pos.z = c->GetPositionZ();
                t.entry = c->GetEntry();
                t.loaded = true;
                t.boss = IsBossEntry(t.entry, c->GetCreatureInfo());
            }
            else
            {
                // Loaded grid without the creature: not spawned now (pool, event, linked spawn).
                if (map->IsLoaded(s.pos.x, s.pos.y))
                    continue;
                if (state && state->GetCreatureRespawnTime(s.dbGuid) > now)
                    continue;
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(s.entry);
                if (!IsTargetTemplate(info, viewer))
                    continue;
                t.pos = s.pos;
                t.boss = IsBossEntry(s.entry, info);
            }
            out.push_back(t);
        }
        return out;
    }
}
}
