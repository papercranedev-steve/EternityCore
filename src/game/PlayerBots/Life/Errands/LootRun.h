/*
 * Bot daily life (PlayerBots/Life), errand: walk to corpses after a fight and take what the
 * server lets this player take, as a player does: its round robin turns, items under the
 * threshold, its own quest items, money (shared with the group by the money handler).
 * Soul companions doc §4.2 (stage S2). Any bot kind can run it; who does is the caller's
 * choice (PartyBotAI: recruited companions, PartyBotAI::LootsForSelf).
 *
 * Update (bot update, out of combat):
 *   - nothing while a group member fights, while mounted or the anchor is mounted (moving
 *     on), or while the bot does not stand (eating, drinking);
 *   - the nearest corpse within SEARCH_YD of the bot and ANCHOR_YD of the anchor (the leader;
 *     the bot itself without one) it may loot (Player::IsAllowedToLoot) with something worth
 *     taking (Shared/LootFacts.h), with a safe walk (Commander/Perception/Spatial/SafeRoute:
 *     no idle pack on the way);
 *   - walks there, opens the corpse (Player::SendLoot), takes the money and every item it may
 *     take through the client's own handlers (all loot rules apply), releases it and puts on
 *     what it looted that is an upgrade (Shared/Inventory.h);
 *   - a corpse it looted, could not reach in GIVE_UP_MS, or had no safe walk to is not tried
 *     again (bags full: the rest stays for the player).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_LOOT_RUN_H
#define MANGOS_LIFE_LOOT_RUN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct LootRunState
    {
        ObjectGuid corpse;          // the corpse walked to
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        Errands::DoneList done;    // corpses not tried again (looted, unreachable)
    };

    namespace LootRun
    {
        constexpr float SEARCH_YD = 25.0f;
        constexpr float ANCHOR_YD = 30.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 SEARCH_MS = 1500;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 15000;
        constexpr size_t DONE_KEEP = 16;
        // Gray items are taken while more bag slots than this are free (they sell at a vendor).
        constexpr uint32 JUNK_ROOM_SLOTS = 4;

        // `anchor`: corpses near it only (the party leader); null = near the bot.
        // True: the bot walks to a corpse or looted one this tick (the caller does nothing
        // else with its movement this tick).
        bool Update(Player* me, LootRunState& state, Player const* anchor);

        // The walk ends (combat, the bot no longer loots).
        void Stop(Player* me, LootRunState& state);
    }
}

#endif
