/*
 * Shared bot facts (PlayerBots/Shared): how a caster's spell damage splits over the magic
 * schools, by class and talents. A frost mage casts Frostbolt and Blizzard, a little Arcane
 * Explosion and Fire Blast: "+Frost damage" is worth most of "+spell damage" to him, "+Fire
 * damage" almost nothing. Gear (Shared/GearFit) weighs "+damage to <schools>" by these shares,
 * and the mage rotation picks its filler by the same main school, so the two agree.
 *
 * Any thread (reads the player's learned spells and static data).
 */

#ifndef MANGOS_BOTSHARED_DAMAGE_SCHOOLS_H
#define MANGOS_BOTSHARED_DAMAGE_SCHOOLS_H

#include "Common.h"
#include "Spells/SpellDefines.h"
#include <array>

class Player;

namespace BotShared
{
    namespace DamageSchools
    {
        // Share of the spell damage done in each SpellSchools (index), summing to 1; all 0 for a
        // class without damage spells.
        using Shares = std::array<float, MAX_SPELL_SCHOOL>;

        // By the class and the talent tree with most points.
        Shares Of(Player const* player);
        // By the class only (a player without talents yet, a class-wide guess).
        Shares OfClass(uint8 classId);

        // The school the character's main damage spell is (mage: fire build = Fireball, else
        // Frostbolt). SPELL_SCHOOL_NORMAL for a class without one.
        SpellSchools Main(Player const* player);

        // Share of the damage a "+damage to <schoolMask>" bonus raises.
        float Covered(Shares const& shares, uint32 schoolMask);
    }
}

#endif
