#include "Shared/GearSources.h"
#include "ItemPrototype.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Spells/SpellAuraDefines.h"
#include "Database/DatabaseEnv.h"
#include "ObjectMgr.h"
#include "SQLStorages.h"
#include "Map.h"
#include "World.h"
#include "Log.h"
#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace BotShared
{
namespace GearSources
{
    namespace
    {
        struct SpellFacts
        {
            std::unordered_set<uint32> crafted;     // SPELL_EFFECT_CREATE_ITEM results
            std::unordered_set<uint32> tools;       // items a spell needs at hand (Mining Pick, Blacksmith Hammer...)
        };

        SpellFacts BuildSpellFacts()
        {
            SpellFacts facts;
            for (uint32 id = 0; id < sSpellMgr.GetMaxSpellId(); ++id)
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(id);
                if (!spell)
                    continue;
                for (uint32 tool : spell->Totem)
                    if (tool)
                        facts.tools.insert(tool);
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->Effect[i] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[i])
                        facts.crafted.insert(uint32(spell->EffectItemType[i]));
            }
            return facts;
        }

        // Spells are loaded before any bot logs in; built once on first use.
        SpellFacts const& Facts()
        {
            static SpellFacts const facts = BuildSpellFacts();
            return facts;
        }

        bool RaisesFishing(ItemPrototype const* proto)
        {
            for (auto const& itemSpell : proto->Spells)
            {
                SpellEntry const* spell = itemSpell.SpellId ? sSpellMgr.GetSpellEntry(itemSpell.SpellId) : nullptr;
                if (!spell)
                    continue;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    uint32 const aura = spell->EffectApplyAuraName[i];
                    if ((aura == SPELL_AURA_MOD_SKILL || aura == SPELL_AURA_MOD_SKILL_TALENT) &&
                        spell->EffectMiscValue[i] == SKILL_FISHING)
                        return true;
                }
            }
            return false;
        }

        struct LootSources
        {
            std::unordered_map<uint32, uint32> sources;                 // item -> Source bits
            std::unordered_map<uint32, std::vector<uint32>> maps;       // item -> dungeon / raid maps
        };

        // A reference pool used by at least this many creatures on the continents is a world drop
        // pool (as Shared/WorldDrops): dungeon trash shares them, they are not the dungeon's loot.
        uint32 const WORLD_POOL_CREATURES = 10;
        uint32 const MAX_REFERENCE_DEPTH = 4;

        using References = std::unordered_map<uint32, std::vector<int32>>;   // ref -> item (> 0) or ref (< 0)

        void CollectReference(References const& refs, uint32 ref, std::unordered_set<uint32> const& skip,
            uint32 depth, std::vector<uint32>& out)
        {
            if (depth > MAX_REFERENCE_DEPTH || skip.count(ref))
                return;
            auto itr = refs.find(ref);
            if (itr == refs.end())
                return;
            for (int32 entry : itr->second)
            {
                if (entry > 0)
                    out.push_back(uint32(entry));
                else
                    CollectReference(refs, uint32(-entry), skip, depth + 1, out);
            }
        }

        void Mark(LootSources& facts, uint32 item, uint32 source, uint32 mapId = 0)
        {
            facts.sources[item] |= source;
            if (mapId)
            {
                std::vector<uint32>& maps = facts.maps[item];
                if (std::find(maps.begin(), maps.end(), mapId) == maps.end())
                    maps.push_back(mapId);
            }
        }

        // The source a loot table on `mapId` gives; 0 = none we count.
        uint32 SourceOfMap(uint32 mapId)
        {
            MapEntry const* map = sMapStorage.LookupEntry<MapEntry>(mapId);
            if (!map)
                return 0;
            if (map->IsContinent())
                return SOURCE_WORLD_DROP;
            if (map->IsRaid())
                return SOURCE_RAID;
            if (map->IsNonRaidDungeon())
                return SOURCE_DUNGEON;
            if (map->IsBattleGround())
                return SOURCE_PVP;
            return 0;
        }

        using LootMaps = std::unordered_map<uint32, std::unordered_set<uint32>>;   // loot id -> maps spawned on

        // Rows (entry, item, mincountOrRef) of a loot table: each loot id's items, sources by the
        // maps its owners are spawned on. In an instance the world drop pools stay world drops.
        void ReadLoot(LootSources& facts, References const& refs, std::unordered_set<uint32> const& worldPools,
            LootMaps const& lootMaps, char const* table, uint32 patch)
        {
            std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                "SELECT `entry`, `item`, `mincountOrRef` FROM `%s` WHERE `patch_min` <= %u AND `patch_max` >= %u",
                table, patch, patch);
            if (!result)
                return;
            std::unordered_set<uint32> const none;
            do
            {
                Field* fields = result->Fetch();
                auto owners = lootMaps.find(fields[0].GetUInt32());
                if (owners == lootMaps.end())
                    continue;
                uint32 const item = fields[1].GetUInt32();
                int32 const ref = fields[2].GetInt32();
                for (uint32 mapId : owners->second)
                {
                    uint32 const source = SourceOfMap(mapId);
                    if (!source)
                        continue;
                    uint32 const instanceMap = (source == SOURCE_DUNGEON || source == SOURCE_RAID) ? mapId : 0;
                    if (ref >= 0)
                    {
                        Mark(facts, item, source, instanceMap);
                        continue;
                    }
                    std::vector<uint32> items;
                    CollectReference(refs, uint32(-ref), source != SOURCE_WORLD_DROP ? worldPools : none, 0, items);
                    for (uint32 i : items)
                        Mark(facts, i, source, instanceMap);
                }
            } while (result->NextRow());
        }

        LootSources BuildLootSources()
        {
            LootSources facts;
            uint32 const patch = sWorld.GetWowPatch();

            References refs;
            if (std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                    "SELECT `entry`, `item`, `mincountOrRef` FROM `reference_loot_template`"
                    " WHERE `patch_min` <= %u AND `patch_max` >= %u", patch, patch))
            {
                do
                {
                    Field* fields = result->Fetch();
                    int32 const ref = fields[2].GetInt32();
                    refs[fields[0].GetUInt32()].push_back(ref < 0 ? ref : int32(fields[1].GetUInt32()));
                } while (result->NextRow());
            }

            // Where each creature loot id and chest loot id is spawned (the loaded spawns: no join).
            LootMaps creatureLoot, chestLoot;
            auto creatureSpawn = [&](CreatureDataPair const& pair)
            {
                CreatureData const& d = pair.second;
                if (d.spawn_flags & SPAWN_FLAG_DISABLED)
                    return false;
                for (uint32 entry : d.creature_id)
                    if (CreatureInfo const* info = entry ? sObjectMgr.GetCreatureTemplate(entry) : nullptr)
                        if (info->loot_id)
                            creatureLoot[info->loot_id].insert(d.position.mapId);
                return false;
            };
            sObjectMgr.DoCreatureData(creatureSpawn);
            auto objectSpawn = [&](GameObjectDataPair const& pair)
            {
                GameObjectData const& d = pair.second;
                if (d.spawn_flags & SPAWN_FLAG_DISABLED)
                    return false;
                if (GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(d.id))
                    if (uint32 lootId = info->GetLootId())
                        chestLoot[lootId].insert(d.position.mapId);
                return false;
            };
            sObjectMgr.DoGOData(objectSpawn);

            // World drop pools: references in the loot of many creature kinds on the continents.
            std::unordered_set<uint32> worldPools;
            if (std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                    "SELECT `entry`, -`mincountOrRef` FROM `creature_loot_template`"
                    " WHERE `mincountOrRef` < 0 AND `patch_min` <= %u AND `patch_max` >= %u", patch, patch))
            {
                std::unordered_map<uint32, uint32> users;   // ref -> continent loot ids using it
                do
                {
                    Field* fields = result->Fetch();
                    auto owners = creatureLoot.find(fields[0].GetUInt32());
                    if (owners == creatureLoot.end() || !(owners->second.count(0) || owners->second.count(1)))
                        continue;
                    if (++users[fields[1].GetUInt32()] == WORLD_POOL_CREATURES)
                        worldPools.insert(fields[1].GetUInt32());
                } while (result->NextRow());
            }

            ReadLoot(facts, refs, worldPools, creatureLoot, "creature_loot_template", patch);
            ReadLoot(facts, refs, worldPools, chestLoot, "gameobject_loot_template", patch);
            if (std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
                    "SELECT `RewChoiceItemId1`, `RewChoiceItemId2`, `RewChoiceItemId3`, `RewChoiceItemId4`,"
                    " `RewChoiceItemId5`, `RewChoiceItemId6`, `RewItemId1`, `RewItemId2`, `RewItemId3`, `RewItemId4`"
                    " FROM `quest_template` WHERE `patch` <= %u", patch))
            {
                do
                {
                    Field* fields = result->Fetch();
                    for (uint32 i = 0; i < 10; ++i)
                        if (uint32 item = fields[i].GetUInt32())
                            Mark(facts, item, SOURCE_QUEST);
                } while (result->NextRow());
            }

            if (std::unique_ptr<QueryResult> result = WorldDatabase.Query(
                    "SELECT `item` FROM `npc_vendor` UNION SELECT `item` FROM `npc_vendor_template`"))
            {
                do
                    Mark(facts, result->Fetch()[0].GetUInt32(), SOURCE_VENDOR);
                while (result->NextRow());
            }

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [GearSources] %u items with a known source, %u world drop pools.",
                uint32(facts.sources.size()), uint32(worldPools.size()));
            return facts;
        }

        LootSources const& Loot()
        {
            static LootSources const facts = BuildLootSources();
            return facts;
        }

        // Gurubashi Arena rewards: the arena chest event needs other players, there is none in a
        // single player world (user, 2026-11-28). Savage Gladiator gear (Blackrock Depths drops)
        // is not one of them.
        bool IsArenaReward(uint32 itemId)
        {
            switch (itemId)
            {
                case 18706:     // Arena Master (trinket, Short John Mithril's chest)
                case 18709:     // Arena Wristguards
                case 18710:     // Arena Bracers
                case 18711:     // Arena Bands
                case 18712:     // Arena Vambraces
                case 19024:     // Arena Grand Master (Gurubashi Arena chest quest)
                    return true;
                default:
                    return false;
            }
        }
    }

    bool IsOddity(ItemPrototype const* proto)
    {
        if (proto->RequiredSkill)
            return true;
        if (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
            return true;
        if (IsArenaReward(proto->ItemId))
            return true;
        if (Facts().tools.count(proto->ItemId))
            return true;
        return RaisesFishing(proto);
    }

    bool IsCrafted(uint32 itemId)
    {
        return Facts().crafted.count(itemId) != 0;
    }

    uint32 SourcesOf(ItemPrototype const* proto)
    {
        uint32 sources = 0;
        auto itr = Loot().sources.find(proto->ItemId);
        if (itr != Loot().sources.end())
            sources = itr->second;
        if (IsCrafted(proto->ItemId))
            sources |= SOURCE_CRAFTED;
        if (proto->RequiredHonorRank)
            sources |= SOURCE_PVP;
        return sources;
    }

    std::vector<uint32> const& InstanceMapsOf(uint32 itemId)
    {
        static std::vector<uint32> const none;
        auto itr = Loot().maps.find(itemId);
        return itr != Loot().maps.end() ? itr->second : none;
    }

    void Warm()
    {
        Facts();
        Loot();
    }
}
}
