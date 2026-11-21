#include "Shared/Crafting.h"
#include "ObjectMgr.h"
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

    std::vector<SpellEntry const*> KnownRecipesOf(Player const* player, uint32 skill)
    {
        std::vector<SpellEntry const*> recipes;
        for (SpellEntry const* recipe : KnownRecipes(player))
            if (SkillOf(recipe) == skill)
                recipes.push_back(recipe);
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

    uint32 SkillOf(SpellEntry const* recipe)
    {
        if (!recipe)
            return 0;
        SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(recipe->Id);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            if (itr->second->skillId)
                return itr->second->skillId;
        return 0;
    }

    uint8 Difficulty(SpellEntry const* recipe, uint32 skillValue)
    {
        SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(recipe->Id);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (!ability->skillId)
                continue;
            // (as Player::SkillGainChance: grey at max_value, green halfway, yellow at min_value)
            uint32 const grey = ability->max_value;
            uint32 const green = (ability->max_value + ability->min_value) / 2;
            uint32 const yellow = ability->min_value;
            if (grey && skillValue >= grey)
                return 3;
            if (green && skillValue >= green)
                return 2;
            if (yellow && skillValue >= yellow)
                return 1;
            return grey ? 0 : 3;     // (no values: it never raises the skill)
        }
        return 3;
    }

    std::string CategoryOf(SpellEntry const* recipe)
    {
        Product const product = ProductOf(recipe);
        ItemPrototype const* proto = product.itemId ? sObjectMgr.GetItemPrototype(product.itemId) : nullptr;
        if (!proto)
            return "0.0";
        if (SkillOf(recipe) == SKILL_ALCHEMY)
        {
            if (proto->Class == ITEM_CLASS_TRADE_GOODS)
                return "transmute";
            if (proto->Class == ITEM_CLASS_CONSUMABLE)
            {
                SpellEntry const* use = nullptr;
                for (auto const& s : proto->Spells)
                    if (s.SpellId && s.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
                    {
                        use = sSpellMgr.GetSpellEntry(s.SpellId);
                        break;
                    }
                if (use)
                {
                    bool aura = false;
                    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    {
                        if (use->Effect[i] == SPELL_EFFECT_HEAL || use->Effect[i] == SPELL_EFFECT_ENERGIZE ||
                            (use->Effect[i] == SPELL_EFFECT_APPLY_AURA && use->EffectApplyAuraName[i] == SPELL_AURA_SCHOOL_ABSORB))
                            return "potion";
                        aura = aura || use->Effect[i] == SPELL_EFFECT_APPLY_AURA;
                    }
                    if (aura)
                        return use->GetDuration() >= 2 * HOUR * IN_MILLISECONDS ? "flask" : "elixir";
                }
            }
        }
        return std::to_string(proto->Class) + "." + std::to_string(proto->SubClass);
    }

    std::vector<Reagent> ReagentsOf(SpellEntry const* recipe)
    {
        std::vector<Reagent> reagents;
        for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
            if (recipe->Reagent[r] > 0 && recipe->ReagentCount[r])
                reagents.push_back({ uint32(recipe->Reagent[r]), recipe->ReagentCount[r] });
        return reagents;
    }
}
}
