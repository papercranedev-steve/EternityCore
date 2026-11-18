#include "Shared/Crafting.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>

namespace BotShared
{
namespace Crafting
{
    Product ProductOf(SpellEntry const* recipe)
    {
        Product product;
        if (!recipe)
            return product;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (recipe->Effect[i] != SPELL_EFFECT_CREATE_ITEM || !recipe->EffectItemType[i])
                continue;
            product.itemId = uint32(recipe->EffectItemType[i]);
            // As Spell::DoCreateItem: the effect value, at least 1.
            product.perCast = uint32(std::max<int32>(recipe->CalculateSimpleValue(SpellEffectIndex(i)), 1));
            break;
        }
        return product;
    }

    std::vector<SpellEntry const*> KnownRecipes(Player const* player)
    {
        std::vector<SpellEntry const*> recipes;
        for (auto const& entry : player->GetSpellMap())
        {
            if (entry.second.state == PLAYERSPELL_REMOVED || entry.second.disabled || !entry.second.active)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(entry.first);
            if (spell && !spell->IsPassiveSpell() && ProductOf(spell).itemId)
                recipes.push_back(spell);
        }
        return recipes;
    }

    bool OnTheRoad(SpellEntry const* recipe)
    {
        return recipe && !recipe->RequiresSpellFocus;
    }

    uint32 CastsFromBags(Player const* player, SpellEntry const* recipe)
    {
        uint32 casts = UINT32_MAX;
        for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        {
            if (recipe->Reagent[r] <= 0 || !recipe->ReagentCount[r])
                continue;
            casts = std::min(casts, player->GetItemCount(uint32(recipe->Reagent[r])) / recipe->ReagentCount[r]);
        }
        return casts;
    }

    bool HasTools(Player const* player, SpellEntry const* recipe)
    {
        for (uint32 t = 0; t < MAX_SPELL_TOTEMS; ++t)
            if (recipe->Totem[t] && !player->HasItemCount(recipe->Totem[t], 1))
                return false;
        return true;
    }

    SpellCastResult Craft(Player* player, SpellEntry const* recipe)
    {
        Product const product = ProductOf(recipe);
        if (!product.itemId || !player->HasSpell(recipe->Id))
            return SPELL_FAILED_NOT_KNOWN;
        if (!OnTheRoad(recipe))
            return SPELL_FAILED_REQUIRES_SPELL_FOCUS;
        if (!CastsFromBags(player, recipe))
            return SPELL_FAILED_REAGENTS;
        if (!HasTools(player, recipe))
            return SPELL_FAILED_TOTEMS;
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, product.itemId, 1) != EQUIP_ERR_OK)
            return SPELL_FAILED_TOO_MANY_OF_ITEM;
        return player->CastSpell(player, recipe, false);
    }
}
}
