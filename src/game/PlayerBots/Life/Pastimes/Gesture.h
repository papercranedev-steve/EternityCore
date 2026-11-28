/*
 * Bot daily life (PlayerBots/Life), pastimes: a gesture at a real player he sees (user
 * 2026-11-22: a wave behind his back or from a bot running past is wasted).
 *
 * The caller wants a text emote at him (after a buff, leaving him a creature, sorry for one).
 * The bot goes on with what it does and waits up to WAIT_MS for him to look at it
 * (Shared/Gaze). When he does, it stops, turns to him, and after a moment of eye contact
 * (EYE_CONTACT_MIN_MS..MAX) makes the gesture, then stands HOLD_AFTER_MIN_MS..MAX more. He never looks,
 * goes beyond REACH_YD or leaves: no gesture.
 *
 * One gesture at a time per bot (a new one replaces a waiting one). Map thread of the bot.
 */

#ifndef MANGOS_LIFE_PASTIMES_GESTURE_H
#define MANGOS_LIFE_PASTIMES_GESTURE_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;

namespace Life
{
namespace Pastimes
{
    // Per bot (a member of its AI). Plain data.
    struct GestureState
    {
        ObjectGuid to;                  // empty: no gesture wanted
        uint32 textEmote = 0;
        uint32 waitUntilMs = 0;         // he looks before this, or no gesture
        uint32 actAtMs = 0;             // != 0: he looked, the gesture then
        uint32 holdUntilMs = 0;         // != 0: made, standing until then
    };

    namespace Gesture
    {
        constexpr uint32 WAIT_MS = 12 * IN_MILLISECONDS;
        constexpr uint32 EYE_CONTACT_MIN_MS = 600, EYE_CONTACT_MAX_MS = 1400;
        // The emote's animation plays out (~2 s), then a moment more (user 2026-11-28: walking
        // off right after it looked like a machine).
        constexpr uint32 HOLD_AFTER_MIN_MS = 2000, HOLD_AFTER_MAX_MS = 3500;
        constexpr float REACH_YD = 40.0f;

        // A text emote at him once he looks (replaces a gesture not made yet).
        void Want(GestureState& state, ObjectGuid to, uint32 textEmote, uint32 now);
        // True: it stands for the gesture now (its walk was ended; the caller does nothing else
        // with its movement and starts its walk again afterwards). False: no gesture, or still
        // waiting for his look (the bot goes on).
        bool Update(Player* me, GestureState& state, uint32 now);
        // Combat, leaving: no gesture.
        void Drop(GestureState& state);
    }
}
}

#endif
