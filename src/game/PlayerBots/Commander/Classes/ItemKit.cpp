#include "Commander/Classes/ItemKit.h"
#include "TeamBotAI.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        using ItemKit::Consumable;

        // Best first inside each shared cooldown (Held keeps the first held one per category).
        uint32 const HEAL_ITEMS[] =
        {
            19013, 19012, 9421,     // Major Healthstone (Improved Healthstone 2 / 1 / none)
            19011, 19010, 5510,     // Greater Healthstone
            19009, 19008, 5509,     // Healthstone
            19007, 19006, 5511,     // Lesser Healthstone
            19005, 19004, 5512,     // Minor Healthstone
            13446,                  // Major Healing Potion
            3928, 18839,            // Superior Healing Potion, Combat Healing Potion
            1710,                   // Greater Healing Potion
            929,                    // Healing Potion
            858,                    // Lesser Healing Potion
            118,                    // Minor Healing Potion
        };

        uint32 const MANA_ITEMS[] =
        {
            13444,                  // Major Mana Potion
            13443, 18841,           // Superior Mana Potion, Combat Mana Potion
            6149,                   // Greater Mana Potion
            3827,                   // Mana Potion
            3385,                   // Lesser Mana Potion
            2455,                   // Minor Mana Potion
            // Gems before runes: a rune also hurts the user.
            8008, 8007, 5513, 5514, // Mana Ruby, Mana Citrine, Mana Jade, Mana Agate
            12662, 20520,           // Demonic Rune, Dark Rune
        };

        template <size_t N>
        void Collect(TeamBotAI const& bot, uint32 const (&ids)[N], std::vector<Consumable>& out, bool firstOnly)
        {
            Player* me = bot.me;
            std::vector<uint32> categories;
            for (uint32 id : ids)
            {
                if (!me->HasItemCount(id, 1))
                    continue;
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(id);
                if (!proto)
                    continue;
                for (auto const& s : proto->Spells)
                {
                    if (!s.SpellId || s.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                        continue;
                    SpellEntry const* spell = sSpellMgr.GetSpellEntry(s.SpellId);
                    if (!spell)
                        break;
                    uint32 const category = s.SpellCategory ? s.SpellCategory : spell->Category;
                    if (category && std::find(categories.begin(), categories.end(), category) != categories.end())
                        break;      // a better one of the same cooldown is listed
                    if (category)
                        categories.push_back(category);
                    Consumable c;
                    c.proto = proto;
                    c.spell = spell;
                    c.category = category;
                    c.cooldownMs = std::max({ uint32(std::max(0, s.SpellCooldown)),
                                              uint32(std::max(0, s.SpellCategoryCooldown)),
                                              spell->GetRecoveryTime() });
                    out.push_back(c);
                    if (firstOnly)
                        return;
                    break;
                }
            }
        }

        void CollectFor(TeamBotAI const& bot, ItemKit::Use use, std::vector<Consumable>& out, bool firstOnly)
        {
            if (use == ItemKit::Use::Heal)
                Collect(bot, HEAL_ITEMS, out, firstOnly);
            else
                Collect(bot, MANA_ITEMS, out, firstOnly);
        }
    }

    std::vector<ItemKit::Consumable> ItemKit::Held(TeamBotAI const& bot, Use use)
    {
        std::vector<Consumable> out;
        CollectFor(bot, use, out, false);
        return out;
    }

    bool ItemKit::HoldsAny(TeamBotAI const& bot, Use use)
    {
        std::vector<Consumable> out;
        CollectFor(bot, use, out, true);
        return !out.empty();
    }

    float ItemKit::AverageValue(SpellEntry const* spell, uint32 effectIndex)
    {
        float const low = float(spell->EffectBasePoints[effectIndex]) + float(spell->EffectBaseDice[effectIndex]);
        float const high = float(spell->EffectBasePoints[effectIndex]) +
                           float(std::max(int32(spell->EffectBaseDice[effectIndex]), spell->EffectDieSides[effectIndex]));
        return (low + high) / 2.0f;
    }
}
