/*
 * PartyBot Commander layer 1 (eyes): which party totem a spell is, and its element (slot).
 * Tags for the totem assigner (Coordination/Duties/TotemAssigner.h) and the world model facts
 * (PartyMemberFacts::totemSpells).
 *
 * One totem per element per shaman; the same totem from two shamans does not stack (the party
 * aura of the same spell), different ones do. Only the totems that help the party without
 * touching enemies: the ones that hit or pull them (Searing, Magma, Fire Nova, Stoneclaw,
 * Earthbind) would break a control. Tremor is situational (TotemAssigner: an enemy that can
 * fear, charm or sleep us); resistances and cleansing have no module yet.
 */

#ifndef MANGOS_COMMANDER_SHAMAN_TOTEMS_H
#define MANGOS_COMMANDER_SHAMAN_TOTEMS_H

#include "Common.h"
#include "SharedDefines.h"

class SpellEntry;

namespace Commander
{
    enum class TotemKind : uint8
    {
        None,
        ManaSpring,         // water: mana for every mana user
        HealingStream,      // water: health for everyone
        StrengthOfEarth,    // earth: strength (melee)
        Stoneskin,          // earth: less melee damage taken
        Windfury,           // air: extra melee attacks
        GraceOfAir,         // air: agility (melee)
        TranquilAir,        // air: less threat (not for the tank: it is a party aura, the tank gets it too)
        Flametongue,        // fire: fire damage on melee weapons
        Tremor,             // earth: every 4 s removes fear, charm and sleep within 30 yd (spell 8146)
    };

    // What Tremor Totem removes (8146: MECHANIC_CHARM, MECHANIC_FEAR, MECHANIC_SLEEP), 1 << mechanic.
    uint32 const TREMOR_MECHANICS = (1u << MECHANIC_CHARM) | (1u << MECHANIC_FEAR) | (1u << MECHANIC_SLEEP);

    namespace ShamanTotems
    {
        // By the spell's (English) name, as PopulateSpellData. None for any other spell.
        TotemKind KindOf(SpellEntry const* spell);
        TotemSlot SlotOf(TotemKind kind);
    }
}

#endif
