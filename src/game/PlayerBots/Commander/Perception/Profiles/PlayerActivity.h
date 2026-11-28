/*
 * PartyBot Commander layer 1 (eyes): what a player is doing right now, out of a fight (player
 * centric doc block 1, .kiro/docs/2026-11-28-player-pacing.md). Read live from the player, no
 * state, no team: Life and the bot AIs may read it too.
 *
 *   afk        his AFK flag (the client sets it by itself after 5 minutes without input)
 *   looting    a loot window is open (Player::GetLootGuid)
 *   trading    a trade window is open
 *   atNpc      he stands still within INTERACTION_DISTANCE + NPC_SLACK_YD of the friendly NPC
 *              he has selected, one with something to offer (UNIT_NPC_FLAGS: gossip, quests,
 *              vendor, trainer, banker, flight master...). The server is not told when he
 *              closes the window: standing there with it selected counts.
 *   eating / drinking  sitting with a food (SPELL_AURA_MOD_REGEN) / drink
 *              (SPELL_AURA_MOD_POWER_REGEN) aura
 *   sitting    any sit (also eating or drinking)
 *   casting    a cast or channel out of combat (Hearthstone, a profession, fishing)
 *   dead / ghost, mounted, taxi, moving
 * Waits(): he is not about to walk on (busy here, or away).
 */
#ifndef MANGOS_COMMANDER_PLAYER_ACTIVITY_H
#define MANGOS_COMMANDER_PLAYER_ACTIVITY_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;

namespace Commander
{
    struct PlayerActivity
    {
        bool afk = false;
        bool looting = false;
        bool trading = false;
        ObjectGuid atNpc;           // empty = none
        bool eating = false;
        bool drinking = false;
        bool sitting = false;
        bool casting = false;
        bool dead = false;
        bool ghost = false;
        bool mounted = false;
        bool taxi = false;
        bool moving = false;

        // Busy where he is (a window open, eating, drinking, sitting, a cast): he stays a while.
        bool Busy() const
        {
            return looting || trading || !atNpc.IsEmpty() || eating || drinking || sitting || casting;
        }
        // Not about to walk on: busy, or away.
        bool Waits() const { return !dead && (afk || Busy()); }
    };

    namespace PlayerActivities
    {
        constexpr float NPC_SLACK_YD = 2.0f;

        PlayerActivity Read(Player const* player);
    }
}

#endif
