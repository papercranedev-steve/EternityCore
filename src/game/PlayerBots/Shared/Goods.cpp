#include "Shared/Goods.h"
#include "Shared/FoodAndDrink.h"
#include "Shared/Inventory.h"
#include "Shared/PetFood.h"
#include "Player.h"
#include "Item.h"
#include "ItemPrototype.h"

namespace BotShared
{
namespace Goods
{
    bool Matches(Player const* player, Spec const& spec, ItemPrototype const* proto)
    {
        if (!proto)
            return false;
        switch (spec.kind)
        {
            case Kind::Item:
                return proto->ItemId == spec.id;
            case Kind::Ammo:
                return proto->Class == ITEM_CLASS_PROJECTILE && proto->SubClass == spec.id &&
                       proto->RequiredLevel <= player->GetLevel() && player->CanUseAmmo(proto->ItemId) == EQUIP_ERR_OK;
            case Kind::Food:
                return FoodAndDrink::IsUsable(player, proto, FoodAndDrink::Kind::Food);
            case Kind::Drink:
                return FoodAndDrink::IsUsable(player, proto, FoodAndDrink::Kind::Drink);
            case Kind::PetFood:
                return PetFood::Benefit(PetFood::HunterPet(player), proto) > 0;
        }
        return false;
    }

    bool Better(Player const* player, Spec const& spec, ItemPrototype const* a, ItemPrototype const* b)
    {
        switch (spec.kind)
        {
            case Kind::Item:
                return false;
            case Kind::Ammo:
                return a->ItemLevel > b->ItemLevel;
            case Kind::Food:
                return FoodAndDrink::Better(player, a, b, FoodAndDrink::Kind::Food);
            case Kind::Drink:
                return FoodAndDrink::Better(player, a, b, FoodAndDrink::Kind::Drink);
            case Kind::PetFood:
                return PetFood::Better(PetFood::HunterPet(player), a, b);
        }
        return false;
    }

    uint32 CountInBags(Player const* player, Spec const& spec)
    {
        if (spec.kind == Kind::Item)
            return player->GetItemCount(spec.id);
        uint32 count = 0;
        for (Item* item : Inventory::BagItems(player))
            if (Matches(player, spec, item->GetProto()))
                count += item->GetCount();
        return count;
    }

    std::string Describe(Spec const& spec)
    {
        switch (spec.kind)
        {
            case Kind::Item:    return "item " + std::to_string(spec.id);
            case Kind::Ammo:    return "ammo";
            case Kind::Food:    return "food";
            case Kind::Drink:   return "drink";
            case Kind::PetFood: return "pet food";
        }
        return "?";
    }
}
}
