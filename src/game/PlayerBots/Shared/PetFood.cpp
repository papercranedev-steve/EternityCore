#include "Shared/PetFood.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Pet.h"
#include "Item.h"
#include "ItemPrototype.h"

namespace BotShared
{
namespace PetFood
{
    Pet const* HunterPet(Player const* player)
    {
        Pet const* pet = player->GetPet();
        return pet && pet->GetPetType() == HUNTER_PET ? pet : nullptr;
    }

    uint32 Benefit(Pet const* pet, ItemPrototype const* proto)
    {
        if (!pet || !proto || !pet->HaveInDiet(proto))
            return 0;
        return pet->GetCurrentFoodBenefitLevel(proto->ItemLevel);
    }

    bool Better(Pet const* pet, ItemPrototype const* a, ItemPrototype const* b)
    {
        uint32 const benefitA = Benefit(pet, a);
        uint32 const benefitB = Benefit(pet, b);
        if (benefitA != benefitB)
            return benefitA > benefitB;
        return a->SellPrice < b->SellPrice;
    }

    Item* BestInBags(Player const* player, Pet const* pet)
    {
        Item* best = nullptr;
        for (Item* item : Inventory::BagItems(player))
        {
            ItemPrototype const* proto = item->GetProto();
            if (!Benefit(pet, proto))
                continue;
            if (!best || Better(pet, proto, best->GetProto()))
                best = item;
        }
        return best;
    }
}
}
