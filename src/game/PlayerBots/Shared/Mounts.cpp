#include "Shared/Mounts.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"

namespace BotShared
{
namespace Mounts
{
    namespace
    {
        struct RaceMounts
        {
            uint8 race;
            uint32 spells[4];   // 0: no more
        };

        // Apprentice mounts sold by each race's mount vendor (spell_template checked 2026-11-22).
        RaceMounts const g_mounts[] =
        {
            { RACE_HUMAN,    { 470, 472, 6648, 458 } },        // Black Stallion, Pinto, Chestnut Mare, Brown Horse
            { RACE_DWARF,    { 6777, 6898, 6899, 0 } },        // Gray, White, Brown Ram
            { RACE_NIGHTELF, { 8394, 10789, 10793, 0 } },      // Striped Frostsaber, Spotted Frostsaber, Striped Nightsaber
            { RACE_GNOME,    { 10873, 17453, 10969, 0 } },     // Red, Green, Blue Mechanostrider
            { RACE_ORC,      { 580, 6653, 6654, 0 } },         // Timber, Dire, Brown Wolf
            { RACE_UNDEAD,   { 17462, 17463, 17464, 0 } },     // Red, Blue, Brown Skeletal Horse
            { RACE_TAUREN,   { 18990, 18989, 0, 0 } },         // Brown, Gray Kodo
            { RACE_TROLL,    { 10796, 10799, 8395, 0 } },      // Turquoise, Violet, Emerald Raptor
        };
    }

    uint32 RacialMountSpell(uint8 race, uint32 seed)
    {
        for (RaceMounts const& m : g_mounts)
        {
            if (m.race != race)
                continue;
            uint32 count = 0;
            while (count < 4 && m.spells[count])
                ++count;
            return count ? m.spells[seed % count] : 0;
        }
        return 0;
    }

    bool Mount(Player* me, uint32 spell, bool instant)
    {
        if (!spell || me->IsMounted() || me->IsInCombat() || !me->IsAlive())
            return false;
        if (me->IsInDisallowedMountForm() && me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
        return me->CastSpell(me, spell, instant) == SPELL_CAST_OK;
    }
}
}
