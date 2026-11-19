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

        // True when it went through: the item was consumed, its cooldown started, or its spell is
        // being cast or channeled.
        bool UseFromBags(Player* player, uint32 itemId, SpellCastTargets const& targets);
    }
}

#endif
