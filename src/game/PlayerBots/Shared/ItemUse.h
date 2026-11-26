/*
 * Shared bot hands (PlayerBots/Shared): use an item from the bags through the client's path
 * (WorldSession::HandleUseItemOpcode: a player's checks, shared item cooldowns, the item's
 * required target). Was CombatBotBaseAI::UseBagItemWithTargets; Life errands use it too (a quest
 * item on its target, Life/Errands/QuestItemUse).
 *
 * The map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_ITEM_USE_H
#define MANGOS_BOTSHARED_ITEM_USE_H

#include "Common.h"

class Player;
class SpellCastTargets;
class SpellEntry;

namespace BotShared
{
    namespace ItemUse
    {
        // The on-use spell of `itemId`'s first stack in the bags (backpack first), nullptr: none.
        SpellEntry const* OnUseSpell(Player const* player, uint32 itemId);

        // Bag items are refused now: shapeshifted (HandleUseItemOpcode, patch 1.10: a form uses
        // equipped items only; Unit::IsShapeShifted, moonkin not counted). A druid tank's
        // healing potion in bear form failed ~55 times in a row (strat-20261126-232120).
        bool BagItemsBlocked(Player const* player);

        // True when it went through: the item was consumed, its cooldown started, or its spell is
        // being cast or channeled.
        bool UseFromBags(Player* player, uint32 itemId, SpellCastTargets const& targets);
    }
}

#endif
