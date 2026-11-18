#include "Shared/FoodAndDrink.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "World.h"

namespace BotShared
{
namespace FoodAndDrink
{
    namespace
    {
        // Aura tick length as the aura handlers default it (SpellAuras.cpp HandleModRegen,
        // HandleAuraModTotalManaPercentRegen).
        float TickSec(SpellEntry const* spell, uint32 i, uint32 defaultMs)
        {
            uint32 const ms = spell->EffectAmplitude[i] ? spell->EffectAmplitude[i] : defaultMs;
            return ms / 1000.0f;
        }

        float EffectRatePerSec(Player const* player, SpellEntry const* spell, uint32 i, Kind kind)
        {
            float const value = float(spell->CalculateSimpleValue(SpellEffectIndex(i)));
            if (value <= 0.0f)
                return 0.0f;
            switch (spell->EffectApplyAuraName[i])
            {
                // Health per tick out of combat (Player::RegenerateHealth).
                case SPELL_AURA_MOD_REGEN:
                    return kind == Kind::Food ? value / TickSec(spell, i, 5000) * sWorld.getConfig(CONFIG_FLOAT_RATE_HEALTH) : 0.0f;
                // Percent of the maximum health per tick (Aura::PeriodicTick).
                case SPELL_AURA_OBS_MOD_HEALTH:
                    return kind == Kind::Food ? player->GetMaxHealth() * value / 100.0f / TickSec(spell, i, 1000) : 0.0f;
                // Mana per 5 seconds (Player::UpdateManaRegen).
                case SPELL_AURA_MOD_POWER_REGEN:
                    return kind == Kind::Drink && spell->EffectMiscValue[i] == POWER_MANA ?
                           value / 5.0f * sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_MANA) : 0.0f;
                // Percent of the maximum mana per tick.
                case SPELL_AURA_OBS_MOD_MANA:
                    return kind == Kind::Drink ? player->GetMaxPower(POWER_MANA) * value / 100.0f / TickSec(spell, i, 1000) : 0.0f;
                default:
                    return 0.0f;
            }
        }

        bool HasEffectOf(SpellEntry const* spell, Kind kind)
        {
            if (!spell || !spell->HasAuraInterruptFlag(AURA_INTERRUPT_STANDING_CANCELS))
                return false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                switch (spell->EffectApplyAuraName[i])
                {
                    case SPELL_AURA_MOD_REGEN:
                    case SPELL_AURA_OBS_MOD_HEALTH:
                        if (kind == Kind::Food)
                            return true;
                        break;
                    case SPELL_AURA_MOD_POWER_REGEN:
                        if (kind == Kind::Drink && spell->EffectMiscValue[i] == POWER_MANA)
                            return true;
                        break;
                    case SPELL_AURA_OBS_MOD_MANA:
                        if (kind == Kind::Drink)
                            return true;
                        break;
                    default:
                        break;
                }
            return false;
        }
    }

    float RatePerSec(Player const* player, SpellEntry const* spell, Kind kind)
    {
        if (!HasEffectOf(spell, kind))
            return 0.0f;
        float rate = 0.0f;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            rate += EffectRatePerSec(player, spell, i, kind);
        return rate;
    }

    SpellEntry const* ItemSpell(ItemPrototype const* proto, Kind kind)
    {
        if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE)
            return nullptr;
        for (auto const& itemSpell : proto->Spells)
        {
            if (!itemSpell.SpellId || itemSpell.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(itemSpell.SpellId);
            if (HasEffectOf(spell, kind))
                return spell;
        }
        return nullptr;
    }

    bool IsUsable(Player const* player, ItemPrototype const* proto, Kind kind)
    {
        return ItemSpell(proto, kind) && player->CanUseItem(proto) == EQUIP_ERR_OK;
    }

    bool Better(Player const* player, ItemPrototype const* a, ItemPrototype const* b, Kind kind)
    {
        float const rateA = RatePerSec(player, ItemSpell(a, kind), kind);
        float const rateB = RatePerSec(player, ItemSpell(b, kind), kind);
        if (rateA != rateB)
            return rateA > rateB;
        return a->SellPrice < b->SellPrice;
    }

    Item* BestInBags(Player const* player, Kind kind)
    {
        Item* best = nullptr;
        for (Item* item : Inventory::BagItems(player))
        {
            ItemPrototype const* proto = item->GetProto();
            if (!IsUsable(player, proto, kind))
                continue;
            if (!best || Better(player, proto, best->GetProto(), kind))
                best = item;
        }
        return best;
    }

    SpellEntry const* Active(Player const* player, Kind kind)
    {
        AuraType const types[2] = {
            kind == Kind::Food ? SPELL_AURA_MOD_REGEN : SPELL_AURA_MOD_POWER_REGEN,
            kind == Kind::Food ? SPELL_AURA_OBS_MOD_HEALTH : SPELL_AURA_OBS_MOD_MANA,
        };
        for (AuraType type : types)
            for (Aura const* aura : player->GetAurasByType(type))
                if (aura && HasEffectOf(aura->GetSpellProto(), kind))
                    return aura->GetSpellProto();
        return nullptr;
    }
}
}
