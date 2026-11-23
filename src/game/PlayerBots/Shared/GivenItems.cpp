#include "Shared/GivenItems.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Player.h"
#include <mutex>
#include <unordered_set>

namespace BotShared
{
namespace GivenItems
{
    namespace
    {
        std::mutex s_lock;
        std::unordered_set<uint32> s_items;     // item guid (low)
        bool s_loaded = false;

        // Under s_lock.
        void LoadOnce()
        {
            if (s_loaded)
                return;
            s_loaded = true;
            CharacterDatabase.DirectExecute(
                "CREATE TABLE IF NOT EXISTS `custom_bot_given_item` ("
                " `item_guid` INT UNSIGNED NOT NULL,"
                " `char_guid` INT UNSIGNED NOT NULL,"
                " PRIMARY KEY (`item_guid`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8");
            CharacterDatabase.DirectExecute(
                "DELETE g FROM `custom_bot_given_item` g LEFT JOIN `item_instance` i ON i.`guid` = g.`item_guid` "
                "WHERE i.`guid` IS NULL");
            std::unique_ptr<QueryResult> rows(CharacterDatabase.Query("SELECT `item_guid` FROM `custom_bot_given_item`"));
            if (!rows)
                return;
            do
                s_items.insert(rows->Fetch()[0].GetUInt32());
            while (rows->NextRow());
        }
    }

    void Mark(Player const* owner, Item const* item)
    {
        if (!owner || !item || owner->IsSavingDisabled())
            return;
        ItemPrototype const* proto = item->GetProto();
        // (conjured goods are kept anyway: a mage's water from a teammate)
        if (!proto || proto->Quality == ITEM_QUALITY_POOR || (proto->Flags & ITEM_FLAG_CONJURED))
            return;
        uint32 const guid = item->GetGUIDLow();
        {
            std::lock_guard<std::mutex> guard(s_lock);
            LoadOnce();
            if (!s_items.insert(guid).second)
                return;
        }
        CharacterDatabase.PExecute("REPLACE INTO `custom_bot_given_item` (`item_guid`, `char_guid`) VALUES (%u, %u)",
            guid, owner->GetGUIDLow());
    }

    bool IsGiven(Item const* item)
    {
        if (!item)
            return false;
        std::lock_guard<std::mutex> guard(s_lock);
        LoadOnce();
        return s_items.count(item->GetGUIDLow()) != 0;
    }
}
}
