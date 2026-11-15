#include "Commander/Perception/Profiles/PetTaunt.h"
#include "Commander/Perception/Profiles/PetAbilities.h"
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
        // Only the threat itself (PetAbilities::Kind::Threat): Scorpid Poison's threat rides on
        // its damage and the Intimidation stun's on a stun, neither is a pet's threat spell.
        return spell->IsAutocastable() && PetAbilities::Of(spell) == PetAbilities::Kind::Threat;
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
