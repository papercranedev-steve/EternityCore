#include "Shared/Supplies.h"
#include "Shared/Crafting.h"
#include "Shared/Gathering.h"
#include "Shared/PetFood.h"
#include "Shared/Professions.h"
#include "Shared/Poisons.h"
#include "Shared/Portals.h"
#include "Player.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <unordered_set>

namespace BotShared
{
namespace Supplies
{
    namespace
    {
        // SetProvided: player -> goods kinds a teammate gives it.
        std::mutex s_providedLock;
        std::map<ObjectGuid, std::set<Goods::Kind>> s_provided;

        std::set<Goods::Kind> ProvidedTo(ObjectGuid player)
        {
            std::lock_guard<std::mutex> guard(s_providedLock);
            auto itr = s_provided.find(player);
            return itr != s_provided.end() ? itr->second : std::set<Goods::Kind>();
        }

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
        // Made food and drink: a stack (no money spent, only mana and time).
        uint32 const MADE_FOOD_TARGET = 20;
        uint32 const MADE_DRINK_TARGET = 20;
        // Poison vials: one per hand for sure, a few rounds more when reagents are bought.
        uint32 const POISON_CRITICAL = 2;
        uint32 const POISON_TARGET = 10;

        using Needs = std::vector<Need>;

        Need Make(Goods::Spec goods, uint32 target, Priority priority, SpellEntry const* recipe = nullptr)
        {
            Need n;
            n.goods = goods;
            n.target = target;
            n.priority = priority;
            n.recipe = recipe;
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
            auto add = [&byItem](uint32 itemId, uint32 target, Priority priority, uint32 tripBelow)
            {
                auto itr = byItem.find(itemId);
                if (itr == byItem.end())
                {
                    byItem[itemId] = Make(Goods::Exact(itemId), target, priority);
                    byItem[itemId].tripBelow = tripBelow;
                    return;
                }
                itr->second.target = std::max(itr->second.target, target);
                itr->second.priority = std::min(itr->second.priority, priority);
                itr->second.tripBelow = std::max(itr->second.tripBelow, tripBelow);
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
                // Portals are what the party travels by (the talk window's "open a portal"):
                // below half the stock, a trip of its own.
                bool const portal = Portals::IsPortalSpell(spell);
                for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                {
                    if (spell->Reagent[r] <= 0 || !IsSoldByVendors(uint32(spell->Reagent[r])))
                        continue;
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(uint32(spell->Reagent[r]));
                    if (!proto)
                        continue;
                    uint32 const perCast = std::max<uint32>(spell->ReagentCount[r], 1);
                    uint32 const target = std::min<uint32>(perCast * CastsFor(priority), std::max<uint32>(proto->GetMaxStackSize(), 1));
                    add(proto->ItemId, target, priority, portal ? (target + 1) / 2 : 0);
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

        // ---- Gathering tools ----

        // One of each its gathering skills need (Mining Pick, Skinning Knife: Shared/Gathering).
        // Important, not critical: no trip for it alone, bought with the next supplies.
        void ToolNeeds(Player const* player, Needs& out)
        {
            for (uint32 skill : Professions::Primaries())
            {
                if (!player->HasSkill(uint16(skill)))
                    continue;
                uint32 const tool = Gathering::ToolOf(skill);
                if (tool && IsSoldByVendors(tool))
                    out.push_back(Make(Goods::Exact(tool), 1, Priority::Important));
            }
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

        // ---- What it makes itself ----

        // A recipe the bot can keep making while it travels: no workplace, its tools with it,
        // every reagent sold by some vendor (or none: a mage's conjuring).
        bool MakeableOnTheRoad(Player const* player, SpellEntry const* recipe)
        {
            if (!Crafting::OnTheRoad(recipe) || !Crafting::HasTools(player, recipe))
                return false;
            for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                if (recipe->Reagent[r] > 0 && recipe->ReagentCount[r] && !IsSoldByVendors(uint32(recipe->Reagent[r])))
                    return false;
            return true;
        }

        // The known recipe whose product is the best of these goods, or null.
        SpellEntry const* BestRecipeFor(Player const* player, Goods::Spec const& goods)
        {
            SpellEntry const* best = nullptr;
            ItemPrototype const* bestProto = nullptr;
            for (SpellEntry const* recipe : Crafting::KnownRecipes(player))
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(Crafting::ProductOf(recipe).itemId);
                if (!Goods::Matches(player, goods, proto) || !MakeableOnTheRoad(player, recipe))
                    continue;
                if (!bestProto || Goods::Better(player, goods, proto, bestProto))
                {
                    best = recipe;
                    bestProto = proto;
                }
            }
            return best;
        }

        void MadeFoodAndDrinkNeeds(Player const* player, Needs& out)
        {
            Goods::Spec const food = Goods::Of(Goods::Kind::Food);
            if (SpellEntry const* recipe = BestRecipeFor(player, food))
                out.push_back(Make(food, MADE_FOOD_TARGET, Priority::Critical, recipe));
            if (!player->GetMaxPower(POWER_MANA))
                return;
            Goods::Spec const drink = Goods::Of(Goods::Kind::Drink);
            if (SpellEntry const* recipe = BestRecipeFor(player, drink))
                out.push_back(Make(drink, MADE_DRINK_TARGET, Priority::Critical, recipe));
        }

        void PoisonNeeds(Player const* player, Needs& out)
        {
            SpellEntry const* recipe = Poisons::BestRecipe(player);
            if (!recipe || !MakeableOnTheRoad(player, recipe))
                return;
            Goods::Spec const vial = Goods::Exact(Crafting::ProductOf(recipe).itemId);
            out.push_back(Make(vial, POISON_CRITICAL, Priority::Critical, recipe));
            out.push_back(Make(vial, POISON_TARGET, Priority::Important, recipe));
        }

        using Source = void (*)(Player const*, Needs&);
        Source const SOURCES[] = {
            &ReagentNeeds,
            &ExtraNeeds,
            &AmmoNeeds,
            &FoodAndDrinkNeeds,
            &PetFoodNeeds,
            &MadeFoodAndDrinkNeeds,
            &PoisonNeeds,
            &ToolNeeds,
        };

        // Every row of every source, `have` counted.
        Needs AllNeeds(Player const* player)
        {
            Needs all;
            for (Source source : SOURCES)
                source(player, all);
            for (Need& n : all)
                n.have = Goods::CountInBags(player, n.goods);
            return all;
        }

        // The reagents for what a made row is missing, at its priority.
        void AddReagentNeeds(Player const* player, Need const& made, Needs& out)
        {
            uint32 const perCast = std::max<uint32>(Crafting::ProductOf(made.recipe).perCast, 1);
            uint32 const casts = (made.Missing() + perCast - 1) / perCast;
            if (!casts)
                return;
            for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
            {
                if (made.recipe->Reagent[r] <= 0 || !made.recipe->ReagentCount[r])
                    continue;
                uint32 const itemId = uint32(made.recipe->Reagent[r]);
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                if (!proto)
                    continue;
                Need n = Make(Goods::Exact(itemId), made.recipe->ReagentCount[r] * casts, made.priority);
                n.target = std::min<uint32>(n.target, std::max<uint32>(proto->GetMaxStackSize(), 1));
                n.have = Goods::CountInBags(player, n.goods);
                out.push_back(n);
            }
        }

        void SortByPriority(Needs& needs)
        {
            std::stable_sort(needs.begin(), needs.end(), [](Need const& a, Need const& b) { return a.priority < b.priority; });
        }
    }

    std::vector<Goods::Spec> KeptGoods(Player const* player)
    {
        std::set<Goods::Spec> kept;
        for (Need const& n : AllNeeds(player))
        {
            kept.insert(n.goods);
            if (n.recipe)
                for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                    if (n.recipe->Reagent[r] > 0)
                        kept.insert(Goods::Exact(uint32(n.recipe->Reagent[r])));
        }
        return std::vector<Goods::Spec>(kept.begin(), kept.end());
    }

    bool IsSoldByVendors(uint32 itemId)
    {
        std::unordered_set<uint32> const& sold = SoldByVendors();
        return sold.find(itemId) != sold.end();
    }

    std::unordered_set<uint32> const& SoldByVendors()
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
        return sold;
    }

    std::vector<Need> Missing(Player const* player)
    {
        Needs const all = AllNeeds(player);

        std::set<Goods::Spec> made;
        Needs bought;
        for (Need const& n : all)
        {
            if (n.recipe)
            {
                made.insert(n.goods);
                AddReagentNeeds(player, n, bought);
            }
            else
                bought.push_back(n);
        }

        std::set<Goods::Kind> const provided = ProvidedTo(player->GetObjectGuid());
        Needs result;
        for (Need const& n : bought)
            if (n.Missing() && !made.count(n.goods) && !provided.count(n.goods.kind))
                result.push_back(n);
        SortByPriority(result);
        return result;
    }

    void SetProvided(ObjectGuid player, std::set<Goods::Kind> const& kinds)
    {
        std::lock_guard<std::mutex> guard(s_providedLock);
        if (kinds.empty())
            s_provided.erase(player);
        else
            s_provided[player] = kinds;
    }

    std::vector<Need> ToMake(Player const* player)
    {
        std::map<Goods::Spec, Need> byGoods;
        for (Need const& n : AllNeeds(player))
        {
            if (!n.recipe || !n.Missing())
                continue;
            auto itr = byGoods.find(n.goods);
            if (itr == byGoods.end())
            {
                byGoods[n.goods] = n;
                continue;
            }
            itr->second.target = std::max(itr->second.target, n.target);
            itr->second.priority = std::min(itr->second.priority, n.priority);
        }

        Needs result;
        for (auto const& itr : byGoods)
            result.push_back(itr.second);
        SortByPriority(result);
        return result;
    }
}
}
