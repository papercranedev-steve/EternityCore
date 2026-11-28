/*
 * Shared bot facts (PlayerBots/Shared): is an item an upgrade for a bot, and which slot would
 * it go to? Used for need / greed rolls (PartyBotAI), wearing what a bot looted, was given or
 * carries when its role changes (Inventory::WearUpgrades), keeping it (BagSorting), the quest
 * reward it picks (QuestRewards) and the companion trade gate.
 *
 * By fit, not by item level (user 2026-11-28): Shared/GearFit scores the item for the class and
 * the role the bot dresses for (stats, equip spells, armor, weapon damage, its suffix).
 *  - The bot can use it; body armor is of the best armor type it wears (no cloth on a warrior).
 *  - Per slot it may go to, the gain is its score over what it replaces: rings and trinkets
 *    the weaker one, a dual wielder's one-hand weapon the main or the off hand. A two-hand
 *    weapon replaces both hands; a one-hand weapon over a two-hand one counts the best off hand
 *    in the bags with it; nothing goes to the off hand next to a two-hand weapon.
 *  - Shield tanks (warrior, paladin) keep a shield: no two-hand weapon, only shields in the
 *    off hand.
 *  - Weapons of melee fighters and hunters count less while the weapon skill is low (half at
 *    skill 0, full at the level's maximum).
 *  - An upgrade gains more than MIN_GAIN_PCT of the score it replaces (no swapping back and
 *    forth between near equals); an empty slot takes anything that scores.
 *  - Trinkets and relics the score cannot see (use effects, bonuses to one spell) score 0:
 *    between two such, the higher item level.
 * The role: what TeamBotAI publishes (SetRole: its role, the one before an emergency role);
 * not published, the class's usual one (GearFit).
 *
 * Map thread of the player (SetRole / RoleOf: any thread).
 */

#ifndef MANGOS_BOTSHARED_GEAR_UPGRADE_H
#define MANGOS_BOTSHARED_GEAR_UPGRADE_H

#include "Common.h"
#include "ObjectGuid.h"

class Item;
class Player;
struct ItemPrototype;

namespace BotShared
{
    namespace GearUpgrade
    {
        constexpr float MIN_GAIN_PCT = 5.0f;
        // The suffix of an item not rolled yet (a quest reward): the average of its suffixes.
        constexpr uint32 UNKNOWN_SUFFIX = 0xFFFFFFFF;

        // The role (CombatBotRoles) `player` dresses for; ROLE_INVALID forgets it.
        void SetRole(ObjectGuid player, uint8 role);
        uint8 RoleOf(Player const* player);

        struct Upgrade
        {
            float gain = 0.0f;          // score gained; 0 = not an upgrade
            uint8 slot = 255;           // equipment slot it goes to (NULL_SLOT when no upgrade)
            float score = 0.0f;         // its score there (with the off hand it brings)
            float replaced = 0.0f;      // the score of what it replaces
        };

        // `randomPropertyId`: the item's suffix (0 none, UNKNOWN_SUFFIX not rolled yet).
        Upgrade Evaluate(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId);
        // An item it carries (its own suffix; not counted as the off hand it would bring).
        Upgrade Evaluate(Player const* player, Item const* item);

        float UpgradeGain(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId);
        bool IsUpgrade(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId);
        bool IsUpgrade(Player const* player, Item const* item);

        // A plain bag (not a quiver, soul or herb bag) it would wear: a bag slot is empty, or it
        // has more slots than the smallest plain bag it wears (Inventory::WearBiggerBags swaps it in).
        bool IsBagUpgrade(Player const* player, ItemPrototype const* proto);
    }
}

#endif
