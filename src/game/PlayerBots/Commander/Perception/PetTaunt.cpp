#include "Commander/Perception/PetTaunt.h"
#include "Pet.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
namespace PetTaunt
{
    bool IsTauntSpell(SpellEntry const* spell)
    {
        if (!spell->IsAutocastable())
            return false;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spell->Effect[i] == SPELL_EFFECT_ATTACK_ME)
                return true;
            if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT)
                return true;
            if (spell->Effect[i] == SPELL_EFFECT_THREAT && spell->CalculateSimpleValue(SpellEffectIndex(i)) > 0)
                return true;
        }
        return false;
    }

    void Spells(Pet const* pet, std::vector<SpellEntry const*>& out)
    {
        for (auto const& learned : pet->m_petSpells)
        {
            if (learned.second.state == PETSPELL_REMOVED)
                continue;
            if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(learned.first))
                if (IsTauntSpell(spell))
                    out.push_back(spell);
        }
    }

    bool HasAutocast(Pet const* pet)
    {
        std::vector<SpellEntry const*> taunts;
        Spells(pet, taunts);
        for (SpellEntry const* spell : taunts)
            if (std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spell->Id) != pet->m_autospells.end())
                return true;
        return false;
    }
}
}
