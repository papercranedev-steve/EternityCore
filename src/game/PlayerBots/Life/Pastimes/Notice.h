/*
 * Bot daily life (PlayerBots/Life), pastimes: noticing a real player who walks by (user
 * 2026-11-19: bots should take the first step, but not all of them).
 *
 * Only an attentive share of the bots (ATTENTIVE_PCT, fixed per bot) notices anyone. Such a
 * bot, standing at a place and free to look around (PlaceActs::CanLookAround), turns to a real
 * player who comes within NOTICE_YD in line of sight, looks at him for a few seconds and then
 * back. Sometimes it also greets him: a wave, a bow or a salute, now and then as a text emote
 * aimed at him ("X waves at you."), only once he looks at it (Shared/Gaze; he never does
 * during the look: no greeting). The same bot notices the same player once per
 * SAME_PLAYER_MS; whatever bot greets him, the next greeting for him waits GREET_GAP_MS
 * (several bots may look, one greets). Bots, ghosts and invisible players are not noticed.
 *
 * Map thread of the bot (the greeting gap is shared by all bots: guarded).
 */

#ifndef MANGOS_LIFE_PASTIMES_NOTICE_H
#define MANGOS_LIFE_PASTIMES_NOTICE_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <utility>

class Player;

namespace Life
{
namespace Pastimes
{
    struct NoticeState
    {
        bool attentive = false;
        ObjectGuid looking;             // empty: not looking at anyone
        uint32 lookUntilMs = 0;
        uint32 greetAtMs = 0;           // != 0: greet him at this time (after turning)
        uint32 nextCheckMs = 0;
        std::deque<std::pair<ObjectGuid, uint32>> noticed;  // player, when
    };

    namespace Notice
    {
        constexpr uint32 ATTENTIVE_PCT = 35;
        constexpr float NOTICE_YD = 12.0f;
        constexpr uint32 CHECK_MS = 1500;
        constexpr uint32 LOOK_MIN_MS = 2500, LOOK_MAX_MS = 5000;
        constexpr uint32 GREET_PCT = 45;            // of the looks
        constexpr uint32 GREET_TEXT_PCT = 35;       // of the greetings: as a text emote
        constexpr uint32 SAME_PLAYER_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 GREET_GAP_MS = 25 * IN_MILLISECONDS;

        // Once per bot: is it one of the attentive ones (seeded: the same bot stays so).
        void Init(NoticeState& state, uint64 seed);
        // `free`: standing at a place, nothing else to do. True: it is looking at someone now
        // (the caller keeps still); false again once it looked back (the caller faces its place).
        bool Update(Player* me, NoticeState& state, bool free, uint32 now);
        // Stops looking (it walks on, sits down ...).
        void Stop(NoticeState& state);
    }
}
}

#endif
