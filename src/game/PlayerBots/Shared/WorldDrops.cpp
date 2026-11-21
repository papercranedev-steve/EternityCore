#include "Shared/WorldDrops.h"
#include "ObjectMgr.h"
#include "ItemPrototype.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include <algorithm>

namespace BotShared
{
    namespace WorldDrops
    {
        namespace
        {
            std::vector<uint32> s_items;
            bool s_loaded = false;

            void Load()
            {
                s_loaded = true;
                uint32 const patch = sWorld.GetWowPatch();
                std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
                    "SELECT DISTINCT r.`item` FROM `reference_loot_template` r JOIN ("
                    "  SELECT -l.`mincountOrRef` AS `ref` FROM `creature_loot_template` l"
                    "  JOIN `creature` c ON c.`id` = l.`entry` AND c.`map` IN (0, 1)"
                    "  WHERE l.`mincountOrRef` < 0 GROUP BY `ref` HAVING COUNT(DISTINCT l.`entry`) >= %u"
                    ") pools ON pools.`ref` = r.`entry`"
                    " WHERE r.`mincountOrRef` > 0 AND r.`patch_min` <= %u AND r.`patch_max` >= %u",
                    MIN_CREATURES, patch, patch));
                if (!result)
                    return;
                do
                {
                    uint32 const item = result->Fetch()[0].GetUInt32();
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item);
                    if (!proto || proto->Bonding == BIND_WHEN_PICKED_UP || proto->Bonding == BIND_QUEST_ITEM
                        || proto->Class == ITEM_CLASS_QUEST || proto->StartQuest)
                        continue;
                    s_items.push_back(item);
                } while (result->NextRow());
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[WorldDrops] %u world drop items.", uint32(s_items.size()));
            }

            uint32 AsksFor(ItemPrototype const* proto)
            {
                if (proto->RequiredLevel)
                    return proto->RequiredLevel;
                return proto->ItemLevel > 5 ? proto->ItemLevel - 5 : 1;
            }
        }

        std::vector<uint32> For(uint32 quality, uint32 level)
        {
            if (!s_loaded)
                Load();
            uint32 const from = level > LEVELS_BELOW ? level - LEVELS_BELOW : 1;
            std::vector<uint32> out;
            for (uint32 item : s_items)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item);
                if (!proto || proto->Quality != quality)
                    continue;
                uint32 const asks = AsksFor(proto);
                if (asks >= from && asks <= level)
                    out.push_back(item);
            }
            return out;
        }
    }
}
