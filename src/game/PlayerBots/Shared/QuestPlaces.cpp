#include "Shared/QuestPlaces.h"
#include "ObjectMgr.h"
#include "LootMgr.h"
#include "QuestDef.h"
#include "Log.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BotShared
{
    namespace QuestPlaces
    {
        namespace
        {
            // A creature (entry) or a gameobject (entry | GO_BIT).
            using Key = uint64;
            Key const GO_BIT = uint64(1) << 32;
            Key CreatureKey(uint32 entry) { return entry; }
            Key ObjectKey(uint32 entry) { return entry | GO_BIT; }

            std::unordered_map<Key, std::vector<WorldLocation>> s_spawns;
            std::unordered_map<uint32, std::vector<Key>> s_givers;       // quest -> who gives it
            std::unordered_map<uint32, std::vector<Key>> s_enders;       // quest -> who takes it back
            std::unordered_map<uint32, std::vector<Key>> s_itemSources;  // item -> what drops it

            void AddOnce(std::vector<Key>& keys, Key key)
            {
                for (Key k : keys)
                    if (k == key)
                        return;
                keys.push_back(key);
            }

            bool Nearest(std::vector<Key> const& keys, uint32 mapId, float x, float y, Place& out)
            {
                float best = -1.0f;
                for (Key key : keys)
                {
                    auto const itr = s_spawns.find(key);
                    if (itr == s_spawns.end())
                        continue;
                    for (WorldLocation const& at : itr->second)
                    {
                        if (at.mapId != mapId)
                            continue;
                        float const dx = at.x - x;
                        float const dy = at.y - y;
                        float const distSq = dx * dx + dy * dy;
                        if (best >= 0.0f && distSq >= best)
                            continue;
                        best = distSq;
                        out.entry = uint32(key & 0xFFFFFFFF);
                        out.gameObject = (key & GO_BIT) != 0;
                        out.x = at.x;
                        out.y = at.y;
                        out.z = at.z;
                    }
                }
                return best >= 0.0f;
            }

            std::vector<Key> const& KeysOf(std::unordered_map<uint32, std::vector<Key>> const& map, uint32 id)
            {
                static std::vector<Key> const none;
                auto const itr = map.find(id);
                return itr != map.end() ? itr->second : none;
            }

            std::string const& Localized(std::vector<std::string> const* strings, int idx, std::string const& base)
            {
                if (strings && idx >= 0 && idx < int(strings->size()) && !(*strings)[idx].empty())
                    return (*strings)[idx];
                return base;
            }
        }

        void Load()
        {
            // The quests that have a mirror, and the items their objectives need.
            std::unordered_set<uint32> quests;
            std::unordered_set<uint32> items;
            std::unordered_set<Key> wanted;
            for (auto const& itr : sObjectMgr.GetQuestTemplates())
            {
                Quest const* quest = itr.second.get();
                if (!quest || quest->GetQuestId() >= QuestMirror::MIRROR_OFFSET || !QuestMirror::HasMirror(quest->GetQuestId()))
                    continue;
                quests.insert(quest->GetQuestId());
                for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    int32 const id = quest->ReqCreatureOrGOId[i];
                    if (id > 0)
                        wanted.insert(CreatureKey(uint32(id)));
                    else if (id < 0)
                        wanted.insert(ObjectKey(uint32(-id)));
                }
                for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                    if (quest->ReqItemId[i])
                        items.insert(quest->ReqItemId[i]);
            }

            auto relations = [&](QuestRelationsMapBounds bounds, std::unordered_map<uint32, std::vector<Key>>& to, Key key)
            {
                for (auto itr = bounds.first; itr != bounds.second; ++itr)
                {
                    if (!quests.count(itr->second))
                        continue;
                    AddOnce(to[itr->second], key);
                    wanted.insert(key);
                }
            };
            std::vector<uint32> drops;
            auto drop = [&](LootTemplate const* loot, Key key)
            {
                if (!loot)
                    return;
                drops.clear();
                loot->CollectOwnItems(drops);
                for (uint32 item : drops)
                {
                    if (!items.count(item))
                        continue;
                    AddOnce(s_itemSources[item], key);
                    wanted.insert(key);
                }
            };

            for (auto const& itr : sObjectMgr.GetCreatureInfoMap())
            {
                CreatureInfo const* info = itr.second.get();
                if (!info)
                    continue;
                Key const key = CreatureKey(itr.first);
                relations(sObjectMgr.GetCreatureQuestRelationsMapBounds(itr.first), s_givers, key);
                relations(sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(itr.first), s_enders, key);
                if (info->loot_id)
                    drop(LootTemplates_Creature.GetLootFor(info->loot_id), key);
            }
            for (auto const& itr : sObjectMgr.GetGameObjectInfoMap())
            {
                GameObjectInfo const* info = itr.second.get();
                if (!info)
                    continue;
                Key const key = ObjectKey(itr.first);
                relations(sObjectMgr.GetGOQuestRelationsMapBounds(itr.first), s_givers, key);
                relations(sObjectMgr.GetGOQuestInvolvedRelationsMapBounds(itr.first), s_enders, key);
                if (uint32 const lootId = info->GetLootId())
                    drop(LootTemplates_Gameobject.GetLootFor(lootId), key);
            }

            // The spawns of all of them.
            size_t spawnCount = 0;
            auto creatureSpawn = [&](CreatureDataPair const& pair)
            {
                CreatureData const& d = pair.second;
                if (d.spawn_flags & (SPAWN_FLAG_DISABLED | SPAWN_FLAG_DEAD))
                    return false;
                for (uint32 entry : d.creature_id)
                {
                    if (!entry || !wanted.count(CreatureKey(entry)))
                        continue;
                    s_spawns[CreatureKey(entry)].push_back(d.position);
                    ++spawnCount;
                }
                return false;
            };
            sObjectMgr.DoCreatureData(creatureSpawn);
            auto objectSpawn = [&](GameObjectDataPair const& pair)
            {
                GameObjectData const& d = pair.second;
                if ((d.spawn_flags & SPAWN_FLAG_DISABLED) || !wanted.count(ObjectKey(d.id)))
                    return false;
                s_spawns[ObjectKey(d.id)].push_back(d.position);
                ++spawnCount;
                return false;
            };
            sObjectMgr.DoGOData(objectSpawn);

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [QuestPlaces] %u quests, %u places, %u spawns.",
                uint32(quests.size()), uint32(s_spawns.size()), uint32(spawnCount));
        }

        bool NearestGiver(Quest const* quest, uint32 mapId, float x, float y, Place& out)
        {
            return quest && Nearest(KeysOf(s_givers, quest->GetQuestId()), mapId, x, y, out);
        }

        bool NearestEnder(Quest const* quest, uint32 mapId, float x, float y, Place& out)
        {
            return quest && Nearest(KeysOf(s_enders, quest->GetQuestId()), mapId, x, y, out);
        }

        bool NearestFor(Quest const* quest, QuestMirror::Counter const& counter, uint32 mapId, float x, float y, Place& out)
        {
            if (!quest)
                return false;
            switch (counter.kind)
            {
                case QuestMirror::Counter::Kind::CreatureOrGO:
                {
                    int32 const id = quest->ReqCreatureOrGOId[counter.index];
                    if (!id)
                        return false;
                    std::vector<Key> const keys{ id > 0 ? CreatureKey(uint32(id)) : ObjectKey(uint32(-id)) };
                    return Nearest(keys, mapId, x, y, out);
                }
                case QuestMirror::Counter::Kind::Item:
                    return Nearest(KeysOf(s_itemSources, quest->ReqItemId[counter.index]), mapId, x, y, out);
                case QuestMirror::Counter::Kind::Done:
                    return false;
            }
            return false;
        }

        std::string NameOf(Place const& place, int localeIdx)
        {
            if (place.gameObject)
            {
                GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(place.entry);
                std::string const base = info ? info->name : "";
                GameObjectLocale const* locale = sObjectMgr.GetGameObjectLocale(place.entry);
                return Localized(locale ? &locale->Name : nullptr, localeIdx, base);
            }
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(place.entry);
            std::string const base = info ? info->name : "";
            CreatureLocale const* locale = sObjectMgr.GetCreatureLocale(place.entry);
            return Localized(locale ? &locale->Name : nullptr, localeIdx, base);
        }
    }
}
