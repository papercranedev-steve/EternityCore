#include "Shared/GearSources.h"
#include "ItemPrototype.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Spells/SpellAuraDefines.h"
#include <unordered_set>

namespace BotShared
{
namespace GearSources
{
    namespace
    {
        struct SpellFacts
        {
            std::unordered_set<uint32> crafted;     // SPELL_EFFECT_CREATE_ITEM results
            std::unordered_set<uint32> tools;       // items a spell needs at hand (Mining Pick, Blacksmith Hammer...)
        };

        SpellFacts BuildSpellFacts()
        {
            SpellFacts facts;
            for (uint32 id = 0; id < sSpellMgr.GetMaxSpellId(); ++id)
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(id);
                if (!spell)
                    continue;
                for (uint32 tool : spell->Totem)
                    if (tool)
                        facts.tools.insert(tool);
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->Effect[i] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[i])
                        facts.crafted.insert(uint32(spell->EffectItemType[i]));
            }
            return facts;
        }

        // Spells are loaded before any bot logs in; built once on first use.
        SpellFacts const& Facts()
        {
            static SpellFacts const facts = BuildSpellFacts();
            return facts;
        }

        bool RaisesFishing(ItemPrototype const* proto)
        {
            for (auto const& itemSpell : proto->Spells)
            {
                SpellEntry const* spell = itemSpell.SpellId ? sSpellMgr.GetSpellEntry(itemSpell.SpellId) : nullptr;
                if (!spell)
                    continue;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    uint32 const aura = spell->EffectApplyAuraName[i];
                    if ((aura == SPELL_AURA_MOD_SKILL || aura == SPELL_AURA_MOD_SKILL_TALENT) &&
                        spell->EffectMiscValue[i] == SKILL_FISHING)
                        return true;
                }
            }
            return false;
        }

        bool IsArenaReward(uint32 itemId)
        {
            switch (itemId)
            {
                case 18706:     // Arena Master (trinket, Short John Mithril's chest)
                case 18709:     // Arena Wristguards
                case 18710:     // Arena Bracers
                case 18711:     // Arena Bands
                case 18712:     // Arena Vambraces
                case 19024:     // Arena Grand Master (Gurubashi Arena chest quest)
                    return true;
                default:
                    return false;
            }
        }
    }

    bool IsOddity(ItemPrototype const* proto)
    {
        if (proto->RequiredSkill)
            return true;
        if (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
            return true;
        if (IsArenaReward(proto->ItemId))
            return true;
        if (Facts().tools.count(proto->ItemId))
            return true;
        return RaisesFishing(proto);
    }

    bool IsCrafted(uint32 itemId)
    {
        return Facts().crafted.count(itemId) != 0;
    }
}
}
