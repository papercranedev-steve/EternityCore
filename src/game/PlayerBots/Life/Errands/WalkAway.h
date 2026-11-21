/*
 * Bot daily life (PlayerBots/Life): a bot about to leave the world while a player watches it
 * leaves as a person does instead of vanishing (design doc .kiro/docs/2026-11-21-companion-
 * presence.md §6, user 2026-11-21): a companion sent away from the party says goodbye, then
 * uses its hearthstone (ready: really cast), else walks off out of his sight; then the caller
 * logs it out. Population bots walk out of sight their own way (Population/, CityDirector).
 *
 *  - Begin: a whisper to the watcher (it is out of the party already): "那我先回暴风城了，有事
 *    再叫我。" with the hearthstone, "那我先走了，有事再叫我。" walking.
 *  - Done (Update false): out of his sight, home after the hearthstone, he left the map, or
 *    MAX_MS passed (it leaves anyway). A broken cast: it walks.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_WALK_AWAY_H
#define MANGOS_LIFE_WALK_AWAY_H

#include "Common.h"
#include "SharedDefines.h"
#include <string>
#include <vector>

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct WalkAwayState
    {
        enum class Phase : uint8
        {
            None,
            Hearthing,
            Walking,
        };
        Phase phase = Phase::None;
        uint32 untilMs = 0;
        uint32 nextMoveMs = 0;
        WorldLocation spot;
    };

    namespace WalkAway
    {
        constexpr uint32 MAX_MS = 90 * IN_MILLISECONDS;
        constexpr uint32 REPATH_MS = 3 * IN_MILLISECONDS;

        inline bool IsActive(WalkAwayState const& state) { return state.phase != WalkAwayState::Phase::None; }
        // False: nothing to do (he does not see it, or nowhere to go): log out now.
        bool Begin(Player* me, WalkAwayState& state, Player* watcher, std::vector<std::string> const& tags);
        // True while it is still on its way out of his sight.
        bool Update(Player* me, WalkAwayState& state, Player* watcher);
    }
}

#endif
