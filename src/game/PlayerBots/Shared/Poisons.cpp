#include "Shared/Poisons.h"
#include "Shared/Crafting.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"

namespace BotShared
{
namespace Poisons
{
    namespace
    {
        // Best kind first: the worth's thousands.
        char const* const DAMAGE_POISONS[] = { "Instant Poison", "Deadly Poison" };
        uint32 const KIND_STEP = 1000;

        // The weapon enchant the vial puts on, or null.
        SpellEntry const* WeaponSpell(ItemPrototype const* proto)
        {
            for (auto const& s : proto->Spells)
            {
                if (!s.SpellId || s.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(s.SpellId);
                if (spell && spell->HasEffect(SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY))
                    return spell;
            }
            return nullptr;
        }
    }

    uint32 Worth(Player const* player, ItemPrototype const* proto)
    {
        if (!proto)
            return 0;
        SpellEntry const* spell = WeaponSpell(proto);
        if (!spell)
            return 0;
        uint32 const kinds = uint32(sizeof(DAMAGE_POISONS) / sizeof(DAMAGE_POISONS[0]));
        for (uint32 k = 0; k < kinds; ++k)
        {
            if (spell->SpellName[0].find(DAMAGE_POISONS[k]) == std::string::npos)
                continue;
            if (player->CanUseItem(proto) != EQUIP_ERR_OK)
                return 0;
            return (kinds - k) * KIND_STEP + proto->ItemLevel;
        }
        return 0;
    }

    SpellEntry const* BestRecipe(Player const* player)
    {
        SpellEntry const* best = nullptr;
        uint32 bestWorth = 0;
        for (SpellEntry const* recipe : Crafting::KnownRecipes(player))
        {
            uint32 const worth = Worth(player, sObjectMgr.GetItemPrototype(Crafting::ProductOf(recipe).itemId));
            if (worth > bestWorth)
            {
                best = recipe;
                bestWorth = worth;
            }
        }
        return best;
    }

    Item* BestInBags(Player const* player)
    {
        Item* best = nullptr;
        uint32 bestWorth = 0;
        for (Item* item : Inventory::BagItems(player))
        {
            uint32 const worth = Worth(player, item->GetProto());
            if (worth > bestWorth)
            {
                best = item;
                bestWorth = worth;
            }
        }
        return best;
    }
}
}
