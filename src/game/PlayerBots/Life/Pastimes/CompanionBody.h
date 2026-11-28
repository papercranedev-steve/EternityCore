/*
 * Bot daily life (PlayerBots/Life), pastimes: a companion's body language towards the player it
 * follows (player-centric doc .kiro/docs/2026-11-28-player-centric.md block 6, user 2026-11-28).
 * The population bots' pieces, put together for a bot that walks with him all day:
 *
 *  - his text emotes at it are answered (EmoteReply; an insult does not send it off: it stays
 *    with him);
 *  - he selects it (to trade, to talk, to look at it) while it stands near: it turns to him after
 *    a moment (REGARD_MIN_MS..MAX), now and then with a wave or a nod (REGARD_GESTURE_PCT, at
 *    most every REGARD_GESTURE_GAP_MS);
 *  - his level-up: some companions congratulate him, its own: a cheer at him (by personality,
 *    CelebratePct). Gestures wait for his look (Gesture, user 2026-11-22: a wave behind his back
 *    is wasted).
 * Sitting down when he sits is the follow's (Commander/Behaviors/Movement/HumanFollow.h).
 *
 * Out of combat only. Map thread of the bot.
 */

#ifndef MANGOS_LIFE_PASTIMES_COMPANION_BODY_H
#define MANGOS_LIFE_PASTIMES_COMPANION_BODY_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Pastimes/EmoteReply.h"
#include "Life/Pastimes/Gesture.h"
#include <string>
#include <vector>

class Player;

namespace Life
{
namespace Pastimes
{
    // Per companion (a member of its AI). Plain data.
    struct CompanionBodyState
    {
        bool init = false;
        EmoteReplyState reply;
        GestureState gesture;
        uint32 leaderLevel = 0;     // last seen (0: not yet)
        uint32 myLevel = 0;
        ObjectGuid leader;          // whose level is watched
        bool regarded = false;      // turned to him for this selection
        uint32 regardAtMs = 0;      // != 0: turns then
        uint32 nextRegardGestureMs = 0;
    };

    namespace CompanionBody
    {
        constexpr float REGARD_YD = 15.0f;
        constexpr uint32 REGARD_MIN_MS = 400, REGARD_MAX_MS = 900;
        constexpr uint32 REGARD_GESTURE_PCT = 35;
        constexpr uint32 REGARD_GESTURE_GAP_MS = 10 * MINUTE * IN_MILLISECONDS;
        // Congratulating his level-up / cheering its own: percent by personality.
        constexpr uint32 CELEBRATE_PCT = 50, CELEBRATE_CHATTY_PCT = 80, CELEBRATE_QUIET_PCT = 20;

        // `from` emoted at it (PlayerAI::ReceiveEmote).
        void Heard(Player* me, CompanionBodyState& state, Player* from, uint32 textEmote, uint32 now);
        // Every out-of-combat tick with its leader. `free`: nothing more important (no fight, no
        // plan of the Commander, no resurrection first). `canStand`: it may stop a few seconds
        // (not resting, the one it follows not walking). True: it stands for an answer or a
        // gesture (its walk was ended: the caller leaves its movement alone and follows again
        // afterwards).
        bool Update(Player* me, CompanionBodyState& state, Player* leader, bool free, bool canStand,
                    std::vector<std::string> const& tags, uint32 now);
        // Combat, leaving: nothing pending, a dance along stops.
        void Stop(Player* me, CompanionBodyState& state);
    }
}
}

#endif
