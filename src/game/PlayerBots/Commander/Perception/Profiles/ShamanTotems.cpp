#include "Commander/Perception/Profiles/ShamanTotems.h"
#include "SpellEntry.h"

namespace Commander
{
namespace ShamanTotems
{
    namespace
    {
        bool Has(SpellEntry const* spell, char const* name)
        {
            return spell->SpellName[0].find(name) != std::string::npos;
        }
    }

    TotemKind KindOf(SpellEntry const* spell)
    {
        if (!spell || spell->SpellFamilyName != SPELLFAMILY_SHAMAN)
            return TotemKind::None;
        if (Has(spell, "Mana Spring Totem"))
            return TotemKind::ManaSpring;
        if (Has(spell, "Healing Stream Totem"))
            return TotemKind::HealingStream;
        if (Has(spell, "Strength of Earth Totem"))
            return TotemKind::StrengthOfEarth;
        if (Has(spell, "Stoneskin Totem"))
            return TotemKind::Stoneskin;
        if (Has(spell, "Windfury Totem"))
            return TotemKind::Windfury;
        if (Has(spell, "Grace of Air Totem"))
            return TotemKind::GraceOfAir;
        if (Has(spell, "Tranquil Air Totem"))
            return TotemKind::TranquilAir;
        if (Has(spell, "Flametongue Totem"))
            return TotemKind::Flametongue;
        return TotemKind::None;
    }

    TotemSlot SlotOf(TotemKind kind)
    {
        switch (kind)
        {
            case TotemKind::ManaSpring:
            case TotemKind::HealingStream:
                return TOTEM_SLOT_WATER;
            case TotemKind::StrengthOfEarth:
            case TotemKind::Stoneskin:
                return TOTEM_SLOT_EARTH;
            case TotemKind::Windfury:
            case TotemKind::GraceOfAir:
            case TotemKind::TranquilAir:
                return TOTEM_SLOT_AIR;
            case TotemKind::Flametongue:
            default:
                return TOTEM_SLOT_FIRE;
        }
    }
}
}
