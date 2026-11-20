#include "Shared/Provisions.h"
#include "Shared/Crafting.h"
#include "Shared/FoodAndDrink.h"
#include "Shared/Goods.h"
#include "Shared/Inventory.h"
#include "Shared/Supplies.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "Player.h"
#include <algorithm>

namespace BotShared
{
namespace Provisions
{
    namespace
    {
        // Minor .. Major Healthstone, each without and with Improved Healthstone (1 / 2).
        uint32 const HEALTHSTONES[] = {
            5512, 19004, 19005,
            5511, 19006, 19007,
            5509, 19008, 19009,
            5510, 19010, 19011,
            9421, 19012, 19013,
        };

        Goods::Spec SpecOf(Kind kind)
        {
            return Goods::Of(kind == Kind::Drink ? Goods::Kind::Drink : Goods::Kind::Food);
        }

        // A conjured item (a mage's food and water, a healthstone; not a cook's dish) of that
        // kind the taker can use now.
        bool Fits(Player const* taker, Kind kind, ItemPrototype const* proto)
        {
            if (!proto || !(proto->Flags & ITEM_FLAG_CONJURED))
                return false;
            if (kind == Kind::Healthstone)
                return IsHealthstone(proto->ItemId) && taker->CanUseItem(proto) == EQUIP_ERR_OK;
            return Goods::Matches(taker, SpecOf(kind), proto);
        }

        // Food and drink some vendor sells (not conjured), built once.
        std::vector<ItemPrototype const*> const& SoldFoodAndDrink()
        {
            static std::vector<ItemPrototype const*> const sold = []()
            {
                std::vector<ItemPrototype const*> list;
                for (auto const& itr : sObjectMgr.GetItemPrototypeMap())
                {
                    ItemPrototype const* proto = &itr.second;
                    if (proto->Flags & ITEM_FLAG_CONJURED)
                        continue;
                    if (!FoodAndDrink::ItemSpell(proto, FoodAndDrink::Kind::Food) &&
                        !FoodAndDrink::ItemSpell(proto, FoodAndDrink::Kind::Drink))
                        continue;
                    if (Supplies::IsSoldByVendors(proto->ItemId))
                        list.push_back(proto);
                }
                return list;
            }();
            return sold;
        }

        // `a` is the better gift for the taker.
        bool BetterFor(Player const* taker, Kind kind, ItemPrototype const* a, ItemPrototype const* b)
        {
            if (kind == Kind::Healthstone)
                return a->ItemLevel > b->ItemLevel;
            return Goods::Better(taker, SpecOf(kind), a, b);
        }
    }

    bool IsHealthstone(uint32 itemId)
    {
        return std::find(std::begin(HEALTHSTONES), std::end(HEALTHSTONES), itemId) != std::end(HEALTHSTONES);
    }

    bool Uses(Player const* taker, Kind kind)
    {
        return kind != Kind::Drink || taker->GetMaxPower(POWER_MANA) > 0;
    }

    uint32 Held(Player const* taker, Kind kind)
    {
        if (kind != Kind::Healthstone)
            return Goods::CountInBags(taker, SpecOf(kind));
        uint32 count = 0;
        for (uint32 id : HEALTHSTONES)
            count += taker->GetItemCount(id);
        return count;
    }

    bool Short(Player const* taker, Kind kind)
    {
        if (!Uses(taker, kind))
            return false;
        uint32 const held = Held(taker, kind);
        return kind == Kind::Healthstone ? held == 0 : held < STACK;
    }

    uint32 GiftCount(Kind kind)
    {
        return kind == Kind::Healthstone ? 1 : STACK * GIFT_STACKS;
    }

    uint32 ProductOf(SpellEntry const* recipe)
    {
        return Crafting::ProductOf(recipe).itemId;
    }

    SpellEntry const* RecipeFor(Player const* giver, Player const* taker, Kind kind)
    {
        SpellEntry const* best = nullptr;
        ItemPrototype const* bestProto = nullptr;
        for (SpellEntry const* recipe : Crafting::KnownRecipes(giver))
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(ProductOf(recipe));
            if (!Fits(taker, kind, proto))
                continue;
            bool const holds = giver->GetItemCount(proto->ItemId) >= GiftCount(kind);
            bool const makes = Crafting::OnTheRoad(recipe) && Crafting::HasTools(giver, recipe) &&
                               Crafting::CastsFromBags(giver, recipe) > 0;
            if (!holds && !makes)
                continue;
            if (!bestProto || BetterFor(taker, kind, proto, bestProto))
            {
                best = recipe;
                bestProto = proto;
            }
        }
        return best;
    }

    bool MakesOwn(Player const* player, Kind kind)
    {
        for (SpellEntry const* recipe : Crafting::KnownRecipes(player))
            if (Fits(player, kind, sObjectMgr.GetItemPrototype(ProductOf(recipe))))
                return true;
        return false;
    }

    float ShareOfBought(Player const* taker, Kind kind, uint32 itemId)
    {
        if (kind == Kind::Healthstone)
            return 1.0f;
        FoodAndDrink::Kind const fd = kind == Kind::Drink ? FoodAndDrink::Kind::Drink : FoodAndDrink::Kind::Food;
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        float const rate = proto ? FoodAndDrink::RatePerSec(taker, FoodAndDrink::ItemSpell(proto, fd), fd) : 0.0f;
        float best = 0.0f;
        for (ItemPrototype const* sold : SoldFoodAndDrink())
            if (FoodAndDrink::IsUsable(taker, sold, fd))
                best = std::max(best, FoodAndDrink::RatePerSec(taker, FoodAndDrink::ItemSpell(sold, fd), fd));
        return best > 0.0f ? std::min(rate / best, 1.0f) : 1.0f;
    }

    bool GoodEnough(Player const* taker, Kind kind, uint32 itemId)
    {
        return ShareOfBought(taker, kind, itemId) >= GOOD_ENOUGH_SHARE;
    }

    std::vector<Kind> Wanted(Player const* giver, Player const* taker, std::vector<Kind> const& kinds)
    {
        std::vector<Kind> wanted;
        for (Kind kind : kinds)
            if (Short(taker, kind) && RecipeFor(giver, taker, kind))
                wanted.push_back(kind);
        return wanted;
    }

    std::vector<Item*> StacksFor(Player const* giver, uint32 itemId, uint32 count, uint32 maxStacks)
    {
        std::vector<Item*> stacks;
        for (Item* item : Inventory::BagItems(giver))
            if (item->GetEntry() == itemId && item->CanBeTraded())
                stacks.push_back(item);
        std::sort(stacks.begin(), stacks.end(), [](Item const* a, Item const* b) { return a->GetCount() > b->GetCount(); });
        std::vector<Item*> chosen;
        uint32 total = 0;
        for (Item* item : stacks)
        {
            if (total >= count || chosen.size() >= maxStacks)
                break;
            chosen.push_back(item);
            total += item->GetCount();
        }
        return chosen;
    }
}
}
