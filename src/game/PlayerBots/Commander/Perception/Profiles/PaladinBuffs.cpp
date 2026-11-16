#include "Commander/Perception/Profiles/PaladinBuffs.h"
#include "SpellEntry.h"

namespace Commander
{
namespace PaladinBuffs
{
    namespace
    {
        bool Has(SpellEntry const* spell, char const* name)
        {
            return spell->SpellName[0].find(name) != std::string::npos;
        }
    }

    BlessingKind BlessingOf(SpellEntry const* spell)
    {
        if (!spell || spell->SpellFamilyName != SPELLFAMILY_PALADIN)
            return BlessingKind::None;
        if (Has(spell, "Blessing of Wisdom"))
            return BlessingKind::Wisdom;
        if (Has(spell, "Blessing of Might"))
            return BlessingKind::Might;
        if (Has(spell, "Blessing of Kings"))
            return BlessingKind::Kings;
        if (Has(spell, "Blessing of Salvation"))
            return BlessingKind::Salvation;
        if (Has(spell, "Blessing of Light"))
            return BlessingKind::Light;
        if (Has(spell, "Blessing of Sanctuary"))
            return BlessingKind::Sanctuary;
        return BlessingKind::None;
    }

    bool IsGreaterBlessing(SpellEntry const* spell)
    {
        return BlessingOf(spell) != BlessingKind::None &&
               spell->EffectImplicitTargetA[0] == TARGET_UNIT_RAID_AND_CLASS;
    }

    JudgementKind SealOf(SpellEntry const* spell)
    {
        if (!spell || spell->SpellFamilyName != SPELLFAMILY_PALADIN)
            return JudgementKind::None;
        if (Has(spell, "Seal of Wisdom"))
            return JudgementKind::Wisdom;
        if (Has(spell, "Seal of Light"))
            return JudgementKind::Light;
        return JudgementKind::None;
    }

    JudgementKind JudgementOf(SpellEntry const* spell)
    {
        if (!spell || spell->SpellFamilyName != SPELLFAMILY_PALADIN)
            return JudgementKind::None;
        if (Has(spell, "Judgement of Wisdom"))
            return JudgementKind::Wisdom;
        if (Has(spell, "Judgement of Light"))
            return JudgementKind::Light;
        return JudgementKind::None;
    }

    PaladinAuraKind AuraOf(SpellEntry const* spell)
    {
        if (!spell || spell->SpellFamilyName != SPELLFAMILY_PALADIN)
            return PaladinAuraKind::None;
        if (Has(spell, "Devotion Aura"))
            return PaladinAuraKind::Devotion;
        if (Has(spell, "Concentration Aura"))
            return PaladinAuraKind::Concentration;
        if (Has(spell, "Retribution Aura"))
            return PaladinAuraKind::Retribution;
        if (Has(spell, "Sanctity Aura"))
            return PaladinAuraKind::Sanctity;
        if (Has(spell, "Shadow Resistance Aura"))
            return PaladinAuraKind::ShadowResistance;
        if (Has(spell, "Frost Resistance Aura"))
            return PaladinAuraKind::FrostResistance;
        if (Has(spell, "Fire Resistance Aura"))
            return PaladinAuraKind::FireResistance;
        return PaladinAuraKind::None;
    }
}
}
