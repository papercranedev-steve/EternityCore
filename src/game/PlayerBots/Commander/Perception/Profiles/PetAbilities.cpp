#include "Commander/Perception/Profiles/PetAbilities.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Pet.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
namespace PetAbilities
{
    Kind Of(SpellEntry const* spell)
    {
        bool const self = spell->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY;
        bool const damage = SpellTags::DealsDamage(spell);
        bool threatUp = false, threatDown = false, charge = false, reduce = false, speed = false, other = false;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            switch (spell->Effect[i])
            {
                case SPELL_EFFECT_NONE:
                    break;
                case SPELL_EFFECT_THREAT:
                {
                    int32 const value = spell->CalculateSimpleValue(SpellEffectIndex(i));
                    threatUp = threatUp || value > 0;
                    threatDown = threatDown || value < 0;
                    break;
                }
                case SPELL_EFFECT_ATTACK_ME:
                    threatUp = true;
                    break;
                case SPELL_EFFECT_CHARGE:
                    charge = true;
                    break;
                case SPELL_EFFECT_APPLY_AURA:
                    switch (spell->EffectApplyAuraName[i])
                    {
                        case SPELL_AURA_MOD_TAUNT:
                            threatUp = true;
                            break;
                        case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                            if (spell->CalculateSimpleValue(SpellEffectIndex(i)) < 0)
                                reduce = true;
                            else
                                other = true;
                            break;
                        case SPELL_AURA_MOD_INCREASE_SPEED:
                            speed = true;
                            break;
                        default:
                            other = true;
                            break;
                    }
                    break;
                default:
                    other = true;
                    break;
            }
        }
        if (charge)
            return Kind::Charge;
        if (damage)
            return self && SpellTags::AreaRadius(spell) > 0.0f ? Kind::AreaDamage : Kind::Damage;
        // Shell Shield also slows the pet's attacks: the damage cut is what it is for.
        if (reduce && self)
            return Kind::Survival;
        if (other)
            return Kind::Other;
        if (speed && self)
            return Kind::Speed;
        if (threatUp && !threatDown)
            return Kind::Threat;
        if (threatDown && !threatUp)
            return Kind::ThreatDrop;
        return Kind::Other;
    }

    void Learned(Pet const* pet, Kind kind, std::vector<SpellEntry const*>& out)
    {
        for (auto const& learned : pet->m_petSpells)
        {
            if (learned.second.state == PETSPELL_REMOVED || learned.second.active == ACT_PASSIVE)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(learned.first);
            if (spell && !spell->IsPassiveSpell() && Of(spell) == kind)
                out.push_back(spell);
        }
    }

    SpellEntry const* First(Pet const* pet, Kind kind)
    {
        std::vector<SpellEntry const*> spells;
        Learned(pet, kind, spells);
        return spells.empty() ? nullptr : spells.front();
    }

    bool IsAutocast(Pet const* pet, SpellEntry const* spell)
    {
        return std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spell->Id) != pet->m_autospells.end();
    }
}
}
