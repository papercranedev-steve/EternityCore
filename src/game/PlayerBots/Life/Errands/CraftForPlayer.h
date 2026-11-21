/*
 * Bot daily life (PlayerBots/Life), errand: making something for a player with the player's
 * materials (design .kiro/docs/2026-11-21-companion-talk.md §3, workspace root; asked through
 * the talk window's craft window). Who may ask (affinity: Companions/Affinity/Gates Crafting)
 * is the caller's (Companions/Talk); this is how it is done.
 *
 * One item after another, no cap (user 2026-11-21): for each cast the materials of one cast go
 * from the player's bags into the bot's (only the player's: what the bot has of them is never
 * used, user 2026-11-21), the bot casts the recipe itself (its skill can rise), and what it
 * made goes to the player (his bags full: by mail). Materials a cast did not use go back.
 * It stops when the count is made, the materials run out, the player walks away (REACH_YD),
 * either of them fights, the cast fails, or the player says stop; the bot says why and the
 * player gets
 *   PBCRAFT;<bot>;made;<item>;<made so far>;<left: still to make, or casts the materials allow>
 *   PBCRAFT;<bot>;stop;<reason>      (Stop reasons below, by their key)
 *
 * Update every bot update; true while it crafts (the bot stays put, no follow).
 * Map thread of the bot (the player is on the same map).
 */

#ifndef MANGOS_LIFE_CRAFT_FOR_PLAYER_H
#define MANGOS_LIFE_CRAFT_FOR_PLAYER_H

#include "Common.h"
#include "ObjectGuid.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class Player;

namespace Life
{
    struct CraftForPlayerState
    {
        ObjectGuid player;
        uint32 spellId = 0;                     // 0 = nothing to make
        uint32 wanted = 0;                      // 0 = as many as the materials allow
        uint32 made = 0;                        // casts that made something
        bool casting = false;
        uint32 nextCastMs = 0;
        uint32 waitSinceMs = 0;                 // waiting for the recipe to be ready since (0 = not)
        std::map<uint32, uint32> reagentsBefore; // the bot's own count of each material before the hand-over
        uint32 productBefore = 0;
        std::set<ObjectGuid> bagsBefore;        // the bot's items before the cast
        std::vector<std::string> tags;          // its personality, for what it says (Phrases)
    };

    namespace CraftForPlayer
    {
        enum class Reason : uint8
        {
            Done,           // "done": the count is made
            NoMaterials,    // "nomats": the player has no more for one
            Far,            // "far": the player walked away
            Combat,         // "combat"
            Stopped,        // "stopped": the player said stop
            BotBagsFull,    // "bagsfull": no room in the bot's bags for materials or the product
            CastFailed,     // "failed": tools, a forge or anvil nearby, interrupted
            Cooldown,       // "cooldown": the recipe's own cooldown (a transmute)
            Gone,           // "gone": the player left the map or logged out
        };
        char const* ReasonKey(Reason reason);

        constexpr float REACH_YD = 12.0f;           // a little more than talking (Talk::TALK_YD)
        constexpr uint32 BETWEEN_CASTS_MS = 400;
        constexpr uint32 READY_WAIT_MS = 4000;      // not ready longer: the recipe's own cooldown

        // Starts making `count` (0 = all the materials allow) with `recipe`; one already going
        // stops first. False: the player has materials for none (nothing started).
        bool Start(Player* me, CraftForPlayerState& state, Player* player, uint32 spellId, uint32 count,
                   std::vector<std::string> const& tags);
        // True while it crafts.
        bool Update(Player* me, CraftForPlayerState& state);
        // Ends it (its cast too), gives back unused materials and says why.
        void Stop(Player* me, CraftForPlayerState& state, Reason reason);
        bool IsActive(CraftForPlayerState const& state);
    }
}

#endif
