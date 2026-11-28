/*
 * Shared bot facts (PlayerBots/Shared): how well a piece of gear fits a class and role, by its
 * stats, its equip spells (spell damage, healing, hit, crit, defense...), its armor and weapon
 * damage, and the suffix of a random property item ("of the Bear"). A player keeps what fits
 * him: random gear stages pick among the better fitting half (Shared/RandomGear).
 * Comparable within one equipment slot only (no stat budget normalising across slots).
 *
 * Any thread (static data).
 */

#ifndef MANGOS_BOTSHARED_GEARFIT_H
#define MANGOS_BOTSHARED_GEARFIT_H

#include "Common.h"

struct ItemPrototype;

namespace BotShared
{
    namespace GearFit
    {
        // `role`: CombatBotRoles. `randomPropertyId`: the item's suffix (ItemRandomProperties
        // id), 0 = none. 0 = nothing of use to it.
        float Score(ItemPrototype const* proto, uint8 classId, uint8 role, uint32 randomPropertyId = 0);

        // The suffix of `proto` (a random property item) this class and role would keep; ties
        // broken by `roll` (any number). 0 = the item has no random property.
        uint32 BestRandomProperty(ItemPrototype const* proto, uint8 classId, uint8 role, uint32 roll = 0);

        // Score with the best suffix (Score of a plain item).
        float BestScore(ItemPrototype const* proto, uint8 classId, uint8 role);
    }
}

#endif
