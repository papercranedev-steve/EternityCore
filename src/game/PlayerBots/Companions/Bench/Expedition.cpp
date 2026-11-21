#include "Companions/Bench/Expedition.h"
#include "Shared/Crafting.h"
#include "Shared/GatheringYield.h"
#include "Shared/MarketValue.h"
#include "Shared/Professions.h"
#include "SpellMgr.h"
#include "ItemPrototype.h"
#include "Item.h"
#include "LootMgr.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <algorithm>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

namespace Companions
{
    namespace Expedition
    {
        namespace
        {
            namespace P = BotShared::Professions;

            std::string ItemsText(std::vector<std::pair<uint32, uint32>> const& items)
            {
                std::ostringstream out;
                for (auto const& item : items)
                    out << (out.tellp() > 0 ? "," : "") << item.first << ":" << item.second;
                return out.str();
            }

            // A benched companion as saved: skill line -> value, known spells.
            struct Saved
            {
                std::map<uint32, uint32> skills;
                std::vector<uint32> spells;
            };

            Saved LoadSaved(uint32 guid)
            {
                Saved saved;
                std::unique_ptr<QueryResult> skills(CharacterDatabase.PQuery(
                    "SELECT `skill`, `value` FROM `character_skills` WHERE `guid` = %u", guid));
                if (skills)
                {
                    do
                    {
                        Field* f = skills->Fetch();
                        saved.skills[f[0].GetUInt32()] = f[1].GetUInt32();
                    } while (skills->NextRow());
                }
                std::unique_ptr<QueryResult> spells(CharacterDatabase.PQuery(
                    "SELECT `spell` FROM `character_spell` WHERE `guid` = %u AND `disabled` = 0", guid));
                if (spells)
                {
                    do
                    {
                        saved.spells.push_back(spells->Fetch()[0].GetUInt32());
                    } while (spells->NextRow());
                }
                return saved;
            }

            struct Candidate
            {
                uint32 itemId = 0;
                float weight = 0.0f;
                uint32 unit = 0;        // copper one is worth
            };

            // Cloth a companion without a gathering skill brings back, by its level.
            uint32 ClothOf(uint32 level)
            {
                return level < 20 ? 2589    // Linen Cloth
                    : level < 30 ? 2592     // Wool Cloth
                    : level < 40 ? 4306     // Silk Cloth
                    : level < 50 ? 4338     // Mageweave Cloth
                    : 14047;                // Runecloth
            }

            std::vector<Candidate> MaterialsOf(Saved const& saved, uint32 level)
            {
                std::map<uint32, float> weights;
                for (uint32 skill : P::Primaries())
                {
                    auto value = saved.skills.find(skill);
                    if (P::KindOf(skill) != P::Kind::Gathering || value == saved.skills.end() || !value->second)
                        continue;
                    for (BotShared::GatheringYield::Yield const& y : BotShared::GatheringYield::At(skill, value->second))
                        weights[y.itemId] += y.weight;
                }
                if (weights.empty())
                    weights[ClothOf(level)] = 1.0f;

                std::vector<Candidate> out;
                for (auto const& w : weights)
                    if (uint32 const unit = BotShared::MarketValue::AuctionPrice(w.first))
                        out.push_back({ w.first, w.second, unit });
                return out;
            }

            std::vector<Candidate> ConsumablesOf(Saved const& saved)
            {
                std::vector<Candidate> fresh, any;
                std::set<uint32> seen;
                for (uint32 spellId : saved.spells)
                {
                    SpellEntry const* recipe = sSpellMgr.GetSpellEntry(spellId);
                    BotShared::Crafting::Product const product = BotShared::Crafting::ProductOf(recipe);
                    if (!product.itemId || !seen.insert(product.itemId).second)
                        continue;
                    uint32 const skill = BotShared::Crafting::SkillOf(recipe);
                    bool const crafts = P::KindOf(skill) == P::Kind::Crafting || skill == P::FIRST_AID || skill == P::COOKING;
                    auto value = saved.skills.find(skill);
                    if (!crafts || value == saved.skills.end())
                        continue;
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(product.itemId);
                    if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || proto->Bonding == BIND_WHEN_PICKED_UP)
                        continue;
                    uint32 const unit = BotShared::MarketValue::WorthOf(product.itemId);
                    if (!unit)
                        continue;
                    // (what it makes now, not what it has long outgrown)
                    Candidate c{ product.itemId, 1.0f, unit };
                    (BotShared::Crafting::Difficulty(recipe, value->second) < 3 ? fresh : any).push_back(c);
                }
                return fresh.empty() ? any : fresh;
            }

            // Spends about `budget` copper on 1..MAX_KINDS kinds of `candidates` (by weight),
            // stacks no bigger than an item's own, at most MAX_NR_LOOT_ITEMS rows in all.
            // Returns the copper given as items.
            uint32 Fill(std::vector<Candidate> candidates, uint32 budget, std::vector<std::pair<uint32, uint32>>& items)
            {
                uint32 spent = 0;
                if (!budget || candidates.empty())
                    return 0;
                uint32 const kinds = urand(1, std::min<uint32>(MAX_KINDS, uint32(candidates.size())));
                uint32 const perKind = budget / kinds;
                for (uint32 k = 0; k < kinds && !candidates.empty(); ++k)
                {
                    float total = 0.0f;
                    for (Candidate const& c : candidates)
                        total += c.weight;
                    float roll = frand(0.0f, total);
                    auto pick = candidates.begin();
                    for (; pick + 1 != candidates.end(); ++pick)
                    {
                        if (roll < pick->weight)
                            break;
                        roll -= pick->weight;
                    }
                    Candidate const c = *pick;
                    candidates.erase(pick);

                    uint32 count = perKind / c.unit;
                    if (!count && float(c.unit) <= float(perKind) * OVERSHOOT)
                        count = 1;
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(c.itemId);
                    uint32 const stack = proto && proto->Stackable ? proto->Stackable : 1;
                    while (count && items.size() < MAX_NR_LOOT_ITEMS)
                    {
                        uint32 const n = std::min(count, stack);
                        items.emplace_back(c.itemId, n);
                        spent += n * c.unit;
                        count -= n;
                    }
                }
                return spent;
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

        Parcel Pack(uint32 ownerLevel, uint32 benchRank, uint32 companionGuid, uint32 companionLevel)
        {
            Worth const* worth = &WORTH[0];
            for (Worth const& w : WORTH)
                if (ownerLevel >= w.fromLevel)
                    worth = &w;

            float const xpRate = (sWorld.getConfig(CONFIG_FLOAT_RATE_XP_KILL) + sWorld.getConfig(CONFIG_FLOAT_RATE_XP_QUEST)) / 2.0f;
            float const moneyRate = sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY);
            uint32 const shares = sizeof(BENCH_SHARE) / sizeof(BENCH_SHARE[0]);
            float const share = BENCH_SHARE[std::min(benchRank, shares - 1)];

            float const training = float(worth->training) * std::max(0.0f, xpRate);
            float const profit = float(worth->profit) * std::max(0.0f, moneyRate + 4.0f) / 6.0f * share
                * frand(1.0f - PROFIT_SPREAD, 1.0f + PROFIT_SPREAD);

            Parcel parcel;
            Saved const saved = LoadSaved(companionGuid);
            uint32 const materials = Fill(MaterialsOf(saved, companionLevel), uint32(profit * MATERIALS_SHARE), parcel.items);
            uint32 const consumables = Fill(ConsumablesOf(saved), uint32(profit * CONSUMABLES_SHARE), parcel.items);
            float const money = training + profit - float(materials + consumables);
            parcel.money = std::max<uint32>(1, uint32(std::max(0.0f, money) + 0.5f));
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
