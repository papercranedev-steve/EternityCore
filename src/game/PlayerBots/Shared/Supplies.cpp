#include "Shared/Supplies.h"
#include "Shared/PetFood.h"
#include "Player.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

namespace BotShared
{
namespace Supplies
{
    namespace
    {
        // Casts' worth kept of a reagent, by priority.
        uint32 const CASTS_CRITICAL = 5;
        uint32 const CASTS_IMPORTANT = 10;
        uint32 const CASTS_OPTIONAL = 5;
        // Ammo kept for the ranged weapon (three stacks of 200).
        uint32 const AMMO_TARGET = 600;
        // Food and drink: a few for sure, a stack when money and bags allow.
        uint32 const FOOD_CRITICAL = 5;
        uint32 const FOOD_TARGET = 20;
        uint32 const DRINK_CRITICAL = 10;
        uint32 const DRINK_TARGET = 20;
        uint32 const PET_FOOD_TARGET = 10;

        using Needs = std::vector<Need>;

        Need Make(Goods::Spec goods, uint32 target, Priority priority)
        {
            Need n;
            n.goods = goods;
            n.target = target;
            n.priority = priority;
            return n;
        }

        // ---- Spell reagents ----

        bool CreatesOrEnchantsItems(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                switch (spell->Effect[i])
                {
                    case SPELL_EFFECT_CREATE_ITEM:
                    case SPELL_EFFECT_ENCHANT_ITEM:
                    case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
                        return true;
                    default:
                        break;
                }
            return false;
        }

        Priority PriorityOf(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                switch (spell->Effect[i])
                {
                    case SPELL_EFFECT_RESURRECT:
                    case SPELL_EFFECT_RESURRECT_NEW:
                    case SPELL_EFFECT_SELF_RESURRECT:
                        return Priority::Critical;
                    case SPELL_EFFECT_TELEPORT_UNITS:
                    case SPELL_EFFECT_SUMMON_OBJECT_WILD:   // portals
                        return Priority::Optional;
                    default:
                        break;
                }
            // Slow Fall / Levitate and the like: comfort.
            if (spell->HasAura(SPELL_AURA_FEATHER_FALL) || spell->HasAura(SPELL_AURA_WATER_WALK))
                return Priority::Optional;
            return Priority::Important;
        }

        uint32 CastsFor(Priority priority)
        {
            switch (priority)
            {
                case Priority::Critical: return CASTS_CRITICAL;
                case Priority::Optional: return CASTS_OPTIONAL;
                default: return CASTS_IMPORTANT;
            }
        }

        void ReagentNeeds(Player const* player, Needs& out)
        {
            // Two spells wanting the same reagent: the bigger target and the higher priority.
            std::map<uint32, Need> byItem;
            auto add = [&byItem](uint32 itemId, uint32 target, Priority priority)
            {
                auto itr = byItem.find(itemId);
                if (itr == byItem.end())
                {
                    byItem[itemId] = Make(Goods::Exact(itemId), target, priority);
                    return;
                }
                itr->second.target = std::max(itr->second.target, target);
                itr->second.priority = std::min(itr->second.priority, priority);
            };

            // Lower ranks the player has a higher rank of are skipped (Wild Berries once Thornroot is due).
            std::set<uint32> lowerRanks;
            for (auto const& entry : player->GetSpellMap())
                if (entry.second.state != PLAYERSPELL_REMOVED && !entry.second.disabled)
                    if (uint32 prev = sSpellMgr.GetPrevSpellInChain(entry.first))
                        lowerRanks.insert(prev);

            for (auto const& entry : player->GetSpellMap())
            {
                if (entry.second.state == PLAYERSPELL_REMOVED || entry.second.disabled || !entry.second.active)
                    continue;
                if (lowerRanks.count(entry.first))
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(entry.first);
                if (!spell || spell->IsPassiveSpell() || CreatesOrEnchantsItems(spell))
                    continue;
                Priority const priority = PriorityOf(spell);
                for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                {
                    if (spell->Reagent[r] <= 0 || !IsSoldByVendors(uint32(spell->Reagent[r])))
                        continue;
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(uint32(spell->Reagent[r]));
                    if (!proto)
                        continue;
                    uint32 const perCast = std::max<uint32>(spell->ReagentCount[r], 1);
                    uint32 const target = std::min<uint32>(perCast * CastsFor(priority), std::max<uint32>(proto->GetMaxStackSize(), 1));
                    add(proto->ItemId, target, priority);
                }
            }

            for (auto const& itr : byItem)
                out.push_back(itr.second);
        }

        // ---- Items that are no spell's reagent ----

        struct Extra
        {
            uint32 knownSpell;      // the player knows it (passive too)
            uint32 itemId;
            uint32 target;
            Priority priority;
        };
        Extra const EXTRA[] = {
            { 20608, 17030, 5, Priority::Critical },    // Reincarnation (passive): Ankh, used by its self-resurrection
        };

        void ExtraNeeds(Player const* player, Needs& out)
        {
            for (Extra const& extra : EXTRA)
                if (player->HasSpell(extra.knownSpell) && IsSoldByVendors(extra.itemId))
                    out.push_back(Make(Goods::Exact(extra.itemId), extra.target, extra.priority));
        }

        // ---- Ammo ----

        uint32 RangedAmmoSubClass(Player const* player)
        {
            Item const* weapon = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
            ItemPrototype const* proto = weapon ? weapon->GetProto() : nullptr;
            if (!proto || proto->Class != ITEM_CLASS_WEAPON)
                return 0;
            switch (proto->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_GUN:
                    return ITEM_SUBCLASS_BULLET;
                case ITEM_SUBCLASS_WEAPON_BOW:
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    return ITEM_SUBCLASS_ARROW;
                default:
                    return 0;
            }
        }

        void AmmoNeeds(Player const* player, Needs& out)
        {
            if (uint32 const subClass = RangedAmmoSubClass(player))
                out.push_back(Make(Goods::AmmoOf(subClass), AMMO_TARGET, Priority::Critical));
        }

        // ---- Food and drink ----

        void FoodAndDrinkNeeds(Player const* player, Needs& out)
        {
            Goods::Spec const food = Goods::Of(Goods::Kind::Food);
            out.push_back(Make(food, FOOD_CRITICAL, Priority::Critical));
            out.push_back(Make(food, FOOD_TARGET, Priority::Important));
            if (player->GetMaxPower(POWER_MANA))
            {
                Goods::Spec const drink = Goods::Of(Goods::Kind::Drink);
                out.push_back(Make(drink, DRINK_CRITICAL, Priority::Critical));
                out.push_back(Make(drink, DRINK_TARGET, Priority::Important));
            }
        }

        void PetFoodNeeds(Player const* player, Needs& out)
        {
            if (PetFood::HunterPet(player))
                out.push_back(Make(Goods::Of(Goods::Kind::PetFood), PET_FOOD_TARGET, Priority::Important));
        }

        using Source = void (*)(Player const*, Needs&);
        Source const SOURCES[] = {
            &ReagentNeeds,
            &ExtraNeeds,
            &AmmoNeeds,
            &FoodAndDrinkNeeds,
            &PetFoodNeeds,
        };
    }

    bool IsSoldByVendors(uint32 itemId)
    {
        static std::unordered_set<uint32> sold;
        static bool built = false;
        if (!built)
        {
            built = true;
            std::set<uint32> templates;
            auto addList = [](VendorItemData const* list)
            {
                if (!list)
                    return;
                for (VendorItem const* v : list->m_items)
                    if (v)
                        sold.insert(v->item);
            };
            for (auto const& info : sObjectMgr.GetCreatureInfoMap())
            {
                CreatureInfo const* cInfo = info.second.get();
                if (!cInfo || !(cInfo->npc_flags & UNIT_NPC_FLAG_VENDOR))
                    continue;
                addList(sObjectMgr.GetNpcVendorItemList(info.first));
                if (cInfo->vendor_id && templates.insert(cInfo->vendor_id).second)
                    addList(sObjectMgr.GetNpcVendorTemplateItemList(cInfo->vendor_id));
            }
        }
        return sold.find(itemId) != sold.end();
    }

    std::vector<Need> Missing(Player const* player)
    {
        Needs all;
        for (Source source : SOURCES)
            source(player, all);

        Needs result;
        for (Need& n : all)
        {
            n.have = Goods::CountInBags(player, n.goods);
            if (n.Missing())
                result.push_back(n);
        }
        std::stable_sort(result.begin(), result.end(), [](Need const& a, Need const& b) { return a.priority < b.priority; });
        return result;
    }
}
}
