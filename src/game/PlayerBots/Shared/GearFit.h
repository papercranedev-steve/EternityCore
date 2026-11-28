/*
 * Shared bot facts (PlayerBots/Shared): how well a piece of gear fits a class and role, by its
 * stats, its equip spells (stats, spell damage, healing, hit, crit, defense...), its on-use
 * effects weighed by uptime (duration / cooldown), its armor and weapon damage, and the suffix
 * of a random property item ("of the Bear", equip spells in 1.12). A player keeps what fits
 * him: random gear stages pick among the better fitting half (Shared/RandomGear), and what a
 * bot wears, needs, keeps and picks as a quest reward is compared by it (Shared/GearUpgrade).
 * Comparable within one equipment slot only (no stat budget normalising across slots).
 *
 * "+damage to <schools>" counts by the share of the wearer's damage in those schools
 * (Shared/DamageSchools, by talents): a frost mage keeps "of Frozen Wrath", not "of Fiery Wrath".
 *
 * Any thread (static data; Of(player) reads the player's learned spells).
 */

#ifndef MANGOS_BOTSHARED_GEARFIT_H
#define MANGOS_BOTSHARED_GEARFIT_H

#include "Common.h"
#include "Shared/DamageSchools.h"

struct ItemPrototype;
class Player;

namespace BotShared
{
    namespace GearFit
    {
        // Who the gear is scored for. Build it once per decision (Of reads the talents).
        struct Wearer
        {
            uint8 classId = 0;
            uint8 role = 0;                     // CombatBotRoles
            DamageSchools::Shares schools = {};
        };
        // `role`: CombatBotRoles.
        Wearer Of(Player const* player, uint8 role);
        // By the class only (no talents known).
        Wearer OfClass(uint8 classId, uint8 role);

        // `randomPropertyId`: the item's suffix (ItemRandomProperties id), 0 = none.
        // 0 = nothing of use to it.
        float Score(ItemPrototype const* proto, Wearer const& wearer, uint32 randomPropertyId = 0);

        // The suffix of `proto` (a random property item) this wearer would keep; ties broken
        // by `roll` (any number). 0 = the item has no random property.
        uint32 BestRandomProperty(ItemPrototype const* proto, Wearer const& wearer, uint32 roll = 0);

        // Score with the best suffix (Score of a plain item).
        float BestScore(ItemPrototype const* proto, Wearer const& wearer);
        // Score of a suffix not rolled yet (a quest reward): the average over its suffixes
        // (Score of a plain item).
        float ExpectedScore(ItemPrototype const* proto, Wearer const& wearer);
    }
}

#endif
