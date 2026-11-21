#include "Shared/GatheringYield.h"
#include "Shared/Professions.h"
#include "DBCStores.h"
#include "SharedDefines.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include <algorithm>
#include <map>
#include <string>

namespace BotShared
{
    namespace GatheringYield
    {
        namespace
        {
            struct Node
            {
                uint32 skill = 0;
                uint32 req = 0;
                uint32 lootId = 0;
            };

            std::vector<Node> s_nodes;
            bool s_nodesLoaded = false;
            std::map<uint64, std::vector<Yield>> s_cache;     // skill << 32 | tier key

            void LoadNodes()
            {
                s_nodesLoaded = true;
                std::unique_ptr<QueryResult> result(WorldDatabase.Query(
                    "SELECT DISTINCT `data0`, `data1` FROM `gameobject_template` WHERE `type` = 3 AND `data0` > 0 AND `data1` > 0"));
                if (!result)
                    return;
                do
                {
                    Field* f = result->Fetch();
                    LockEntry const* lock = sLockStore.LookupEntry(f[0].GetUInt32());
                    if (!lock)
                        continue;
                    for (int j = 0; j < MAX_LOCK_CASE; ++j)
                    {
                        if (lock->Type[j] != LOCK_KEY_SKILL)
                            continue;
                        SkillType const skill = SkillByLockType(LockType(lock->Index[j]));
                        if (skill == SKILL_HERBALISM || skill == SKILL_MINING)
                            s_nodes.push_back({ uint32(skill), lock->Skill[j], f[1].GetUInt32() });
                        break;
                    }
                } while (result->NextRow());
            }

            // Items per opening of each loot id in `ids`, summed: plain rows of this patch.
            void AddLoot(char const* table, std::vector<uint32> const& ids, std::map<uint32, float>& items)
            {
                if (ids.empty())
                    return;
                std::string list;
                for (uint32 id : ids)
                    list += (list.empty() ? "" : ",") + std::to_string(id);
                uint32 const patch = sWorld.GetWowPatch();
                std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
                    "SELECT `entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount` FROM `%s`"
                    " WHERE `entry` IN (%s) AND `mincountOrRef` > 0 AND `ChanceOrQuestChance` >= 0"
                    " AND `patch_min` <= %u AND `patch_max` >= %u", table, list.c_str(), patch, patch));
                if (!result)
                    return;

                struct Row { uint32 item; float chance; float count; };
                std::map<std::pair<uint32, uint32>, std::vector<Row>> groups;   // (entry, group) -> rows
                do
                {
                    Field* f = result->Fetch();
                    float const count = (float(f[4].GetInt32()) + float(std::max<uint32>(f[5].GetUInt32(), f[4].GetInt32()))) / 2.0f;
                    groups[{ f[0].GetUInt32(), f[3].GetUInt32() }].push_back({ f[1].GetUInt32(), f[2].GetFloat(), count });
                } while (result->NextRow());

                for (auto const& group : groups)
                {
                    bool const plain = group.first.second == 0;
                    float explicitChance = 0.0f;
                    uint32 equal = 0;
                    for (Row const& row : group.second)
                    {
                        if (row.chance > 0.0f)
                            explicitChance += row.chance;
                        else
                            ++equal;
                    }
                    // (group 0: every row on its own, chance 0 = always; other groups: one of
                    // them, the rows without a chance share what the others leave)
                    float const equalChance = plain ? 100.0f : (equal ? std::max(0.0f, 100.0f - explicitChance) / float(equal) : 0.0f);
                    for (Row const& row : group.second)
                    {
                        float const chance = row.chance > 0.0f ? row.chance : equalChance;
                        if (chance > 0.0f)
                            items[row.item] += chance / 100.0f * row.count;
                    }
                }
            }

            std::vector<Yield> ToYields(std::map<uint32, float> const& items)
            {
                std::vector<Yield> yields;
                for (auto const& item : items)
                    if (item.second > 0.0f)
                        yields.push_back({ item.first, item.second });
                return yields;
            }

            std::vector<Yield> Nodes(uint32 skill, uint32 value)
            {
                if (!s_nodesLoaded)
                    LoadNodes();
                // The tier: asked for within TIER of the value, else the highest below it.
                uint32 highest = 0;
                bool any = false;
                for (Node const& node : s_nodes)
                    if (node.skill == skill && node.req <= value)
                    {
                        highest = std::max(highest, node.req);
                        any = true;
                    }
                if (!any)
                    return {};
                uint32 const from = std::min(highest, value > TIER ? value - TIER : 0);
                std::vector<uint32> ids;
                for (Node const& node : s_nodes)
                    if (node.skill == skill && node.req <= value && node.req >= from)
                        ids.push_back(node.lootId);
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
                std::map<uint32, float> items;
                AddLoot("gameobject_loot_template", ids, items);
                return ToYields(items);
            }

            uint32 SkinLevel(uint32 value)
            {
                return value < 100 ? 10 + value / 10 : value / 5;
            }

            std::vector<Yield> Corpses(uint32 value)
            {
                uint32 const top = SkinLevel(value);
                uint32 const bottom = top > SKIN_LEVELS ? top - SKIN_LEVELS : 1;
                std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
                    "SELECT DISTINCT `skinning_loot_id` FROM `creature_template` WHERE `skinning_loot_id` > 0"
                    " AND `level_max` BETWEEN %u AND %u", bottom, top));
                std::vector<uint32> ids;
                if (result)
                {
                    do
                    {
                        ids.push_back(result->Fetch()[0].GetUInt32());
                    } while (result->NextRow());
                }
                std::map<uint32, float> items;
                AddLoot("skinning_loot_template", ids, items);
                return ToYields(items);
            }
        }

        std::vector<Yield> const& At(uint32 skill, uint32 value)
        {
            static std::vector<Yield> const none;
            if (Professions::KindOf(skill) != Professions::Kind::Gathering)
                return none;
            // Same tier, same answer: herbs and ore by the value, skins by the level it skins.
            uint32 const key = skill == SKILL_SKINNING ? SkinLevel(value) : value;
            uint64 const cacheKey = (uint64(skill) << 32) | key;
            auto itr = s_cache.find(cacheKey);
            if (itr != s_cache.end())
                return itr->second;
            std::vector<Yield> yields = skill == SKILL_SKINNING ? Corpses(value) : Nodes(skill, value);
            return s_cache.emplace(cacheKey, std::move(yields)).first->second;
        }
    }
}
