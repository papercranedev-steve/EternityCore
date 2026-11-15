#include "Commander/Perception/Profiles/WarlockCurses.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
namespace WarlockCurses
{
    namespace
    {
        bool Has(SpellEntry const* spell, char const* name)
        {
            return spell->SpellName[0].find(name) != std::string::npos;
        }
    }

    CurseKind KindOf(SpellEntry const* spell)
    {
        if (!spell || spell->SpellFamilyName != SPELLFAMILY_WARLOCK)
            return CurseKind::None;
        if (Has(spell, "Curse of the Elements"))
            return CurseKind::Elements;
        if (Has(spell, "Curse of Shadow"))
            return CurseKind::Shadow;
        if (Has(spell, "Curse of Recklessness"))
            return CurseKind::Recklessness;
        return CurseKind::None;
    }

    CurseEffect EffectOf(SpellEntry const* spell)
    {
        CurseEffect out;
        out.kind = KindOf(spell);
        if (out.kind == CurseKind::None)
            return out;
        out.durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                continue;
            float const value = float(spell->CalculateSimpleValue(SpellEffectIndex(i)));
            switch (spell->EffectApplyAuraName[i])
            {
                case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                    out.damagePct = std::max(out.damagePct, value / 100.0f);
                    break;
                case SPELL_AURA_MOD_RESISTANCE:
                    if ((spell->EffectMiscValue[i] & SPELL_SCHOOL_MASK_NORMAL) && value < 0.0f)
                        out.armorCut = std::max(out.armorCut, -value);
                    break;
                case SPELL_AURA_MOD_ATTACK_POWER:
                    out.apAdded = std::max(out.apAdded, value);
                    break;
                default:
                    break;
            }
        }
        return out;
    }

    CurseKind Favoured(uint8 classId, uint8 role)
    {
        if (role == ROLE_TANK || role == ROLE_HEALER)
            return CurseKind::None;
        switch (classId)
        {
            case CLASS_MAGE:
                return CurseKind::Elements;
            case CLASS_WARLOCK:
            case CLASS_PRIEST:
                return CurseKind::Shadow;
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_PALADIN:
                return CurseKind::Recklessness;
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return role == ROLE_MELEE_DPS ? CurseKind::Recklessness : CurseKind::None;
            default:
                return CurseKind::None;
        }
    }

    std::vector<CurseKind> TieOrder()
    {
        return { CurseKind::Elements, CurseKind::Shadow, CurseKind::Recklessness };
    }

    char const* Name(CurseKind kind)
    {
        switch (kind)
        {
            case CurseKind::Elements: return "Curse of the Elements";
            case CurseKind::Shadow: return "Curse of Shadow";
            case CurseKind::Recklessness: return "Curse of Recklessness";
            default: return "none";
        }
    }
}
}
