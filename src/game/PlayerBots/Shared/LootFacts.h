/*
 * Shared bot facts (PlayerBots/Shared: depends on nothing of the bot layers): what a player could take from a corpse, by the server's own loot
 * rules (Player::IsAllowedToLoot, LootItem::AllowedForPlayer, round robin and threshold).
 * Facts, plus TakeOpenLoot (the hands for an open loot window). Who loots is decided
 * elsewhere (the clear's loot wait, a companion's errands).
 * Used by Commander/Coordination/Situation/ClearPlanner and Life/Errands/LootRun.
 *
 * Map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_LOOT_FACTS_H
#define MANGOS_BOTSHARED_LOOT_FACTS_H

#include "Common.h"
#include <vector>

class Creature;
class Player;
class WorldObject;
struct LootItem;
struct Loot;

namespace BotShared
{
    namespace BagSorting
    {
        struct Rules;
    }

    namespace LootFacts
    {
        // Gray (poor quality) items are vendor trash: the player often leaves them on the corpse.
        bool IsWorthLooting(LootItem const& item);

        // A shared item (not FFA, not conditional) the player may take from the window. When
        // another member is the round robin looter, only items over the group loot threshold
        // (they are rolled).
        bool MayTakeSharedItem(Player const* player, Loot const& loot, LootItem const& item);

        // Something on the corpse the player would take: money, one of their quest items, or an
        // item it may take (above gray quality unless `includePoor`). Items being rolled for
        // (is_blocked) do not need the corpse any more.
        bool HasLootWorthTaking(Player const* player, Creature const* corpse, bool includePoor = false);

        // Dead creatures within `radius` of `center` the client shows as lootable.
        std::vector<Creature*> LootableCorpsesNear(WorldObject const* center, float radius);

        // Loot window slot of every item the player would take now (the numbering of
        // Loot::LootItemInSlot: shared and FFA / conditional items first, then the player's quest
        // items). Same filters as HasLootWorthTaking.
        std::vector<uint8> SlotsToTake(Player const* player, Creature const* corpse, bool includePoor = false);
        // Any loot window (a chest: `target` is the gameobject).
        std::vector<uint8> SlotsToTake(Player const* player, Loot const& loot, WorldObject const* target, bool includePoor = false);

        // The loot window open on the player (Player::GetLootGuid): takes the money (shared with
        // the group by the money handler) and every item SlotsToTake gives, through the client's
        // own handlers, then closes it. True: a window was open.
        // `makeRoom`: an item that does not fit gets room first (Shared/BagSorting::MakeRoomFor:
        // cheaper things in the bags are destroyed; a quest item gets room from anything not
        // kept). Null: what does not fit stays.
        bool TakeOpenLoot(Player* player, bool includePoor = false, BagSorting::Rules const* makeRoom = nullptr);
    }
}

#endif
