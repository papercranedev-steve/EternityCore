#include "Companions/Bench/Expedition.h"
#include "Item.h"
#include "LootMgr.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Database/DatabaseEnv.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <algorithm>
#include <ctime>
#include <sstream>

namespace Companions
{
    namespace Expedition
    {
        namespace
        {
            std::string ItemsText(std::vector<std::pair<uint32, uint32>> const& items)
            {
                std::ostringstream out;
                for (auto const& item : items)
                    out << (out.tellp() > 0 ? "," : "") << item.first << ":" << item.second;
                return out.str();
            }

            std::vector<std::pair<uint32, uint32>> ParseItems(std::string const& text)
            {
                std::vector<std::pair<uint32, uint32>> items;
                std::istringstream in(text);
                std::string entry;
                while (std::getline(in, entry, ','))
                {
                    std::size_t const colon = entry.find(':');
                    if (colon == std::string::npos)
                        continue;
                    uint32 const id = uint32(std::strtoul(entry.substr(0, colon).c_str(), nullptr, 10));
                    uint32 const count = uint32(std::strtoul(entry.substr(colon + 1).c_str(), nullptr, 10));
                    if (id && count)
                        items.emplace_back(id, count);
                }
                return items;
            }
        }

        Parcel Pack(uint32 level)
        {
            Parcel parcel;
            float const base = MOBS_WORTH * MONEY_PER_LEVEL_SQ * float(level * level);
            parcel.money = std::max<uint32>(1, uint32(base * frand(1.0f - MONEY_SPREAD, 1.0f + MONEY_SPREAD) + 0.5f));
            return parcel;
        }

        bool Send(Player* owner, uint32 companionGuid, Parcel const& parcel, std::string const& subject, std::string const& body)
        {
            if (!owner || !companionGuid)
                return false;
            Item* item = Item::CreateItem(PARCEL_ITEM, 1, owner->GetObjectGuid());
            if (!item)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Parcel item %u does not exist, no parcel sent.", PARCEL_ITEM);
                return false;
            }
            item->SaveToDB();
            CharacterDatabase.PExecute("INSERT INTO `custom_companion_parcel` (`item_guid`, `char_guid`, `owner_guid`, `money`, `items`, `sent_at`)"
                " VALUES (%u, %u, %u, %u, '%s', %u)", item->GetGUIDLow(), companionGuid, owner->GetGUIDLow(), parcel.money,
                ItemsText(parcel.items).c_str(), uint32(time(nullptr)));
            MailDraft(subject, body)
                .AddItem(item)
                .SendMailTo(MailReceiver(owner), MailSender(MAIL_NORMAL, companionGuid));
            return true;
        }

        uint32 Unopened(uint32 companionGuid, uint32 ownerGuid)
        {
            std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
                "SELECT COUNT(*) FROM `custom_companion_parcel` p JOIN `item_instance` i ON i.`guid` = p.`item_guid`"
                " WHERE p.`char_guid` = %u AND i.`owner_guid` = %u", companionGuid, ownerGuid));
            return result ? result->Fetch()[0].GetUInt32() : 0;
        }

        void Load()
        {
            CharacterDatabase.DirectExecute(
                "CREATE TABLE IF NOT EXISTS `custom_companion_parcel` ("
                " `item_guid` INT UNSIGNED NOT NULL COMMENT 'item_instance.guid of the parcel',"
                " `char_guid` INT UNSIGNED NOT NULL COMMENT 'the companion that sent it',"
                " `owner_guid` INT UNSIGNED NOT NULL COMMENT 'the player it was sent to',"
                " `money` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'copper',"
                " `items` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'entry:count,entry:count',"
                " `sent_at` INT UNSIGNED NOT NULL DEFAULT 0,"
                " PRIMARY KEY (`item_guid`), KEY `idx_char` (`char_guid`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: expedition parcels not opened yet'");
            // (destroyed unopened, or expired from a returned mail)
            CharacterDatabase.DirectExecute("DELETE p FROM `custom_companion_parcel` p"
                " LEFT JOIN `item_instance` i ON i.`guid` = p.`item_guid` WHERE i.`guid` IS NULL");

            static bool registered = false;
            if (!registered)
            {
                AddItemLootFiller(&FillParcel);
                registered = true;
            }
        }

        bool FillParcel(Item* item, Player* /*opener*/, Loot& loot)
        {
            if (!item || item->GetEntry() != PARCEL_ITEM)
                return false;
            std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
                "SELECT `money`, `items` FROM `custom_companion_parcel` WHERE `item_guid` = %u", item->GetGUIDLow()));
            if (!row)
                return false;
            Field* f = row->Fetch();
            loot.gold = f[0].GetUInt32();
            for (auto const& entry : ParseItems(f[1].GetCppString()))
            {
                if (loot.items.size() >= MAX_NR_LOOT_ITEMS)
                    break;
                if (!sObjectMgr.GetItemPrototype(entry.first))
                    continue;
                loot.items.push_back(LootItem(entry.first, entry.second));
                ++loot.unlootedCount;
            }
            // (now the item's own loot: saved with it from here, Item::SaveToDB)
            CharacterDatabase.PExecute("DELETE FROM `custom_companion_parcel` WHERE `item_guid` = %u", item->GetGUIDLow());
            return true;
        }
    }
}
