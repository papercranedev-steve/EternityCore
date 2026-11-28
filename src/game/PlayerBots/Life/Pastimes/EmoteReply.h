/*
 * Bot daily life (PlayerBots/Life), pastimes: answering a real player's text emote aimed at the
 * bot (/wave, /bow, /rude ... at it; population interactions, user 2026-11-22).
 *
 * The bot turns to him and answers in kind after a short moment:
 *   - a greeting with a greeting, a goodbye with a goodbye;
 *   - thanks, praise, a hug, a kiss with a nod, a smile, a blush;
 *   - an insult (/rude, /spit, /chicken, /slap ...) by its temper (fixed per bot): hot-headed
 *     ones are angry or rude back, timid ones cower and walk off (WalkOff), the others sigh or
 *     shrug;
 *   - a dance: now and then it dances along for a few seconds (only when it may stand still);
 *   - a flex or a roar: a laugh or applause; tears: comfort; pointing, staring: puzzled.
 * The same player emoting at it again and again (more than SPAM_COUNT within SPAM_MS): one sigh,
 * then nothing until he stops. Emotes of bots are not answered.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_PASTIMES_EMOTE_REPLY_H
#define MANGOS_LIFE_PASTIMES_EMOTE_REPLY_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <utility>

class Player;

namespace Life
{
namespace Pastimes
{
    enum class Temper : uint8 { Calm, Hot, Timid };

    struct EmoteReplyState
    {
        Temper temper = Temper::Calm;
        ObjectGuid from;                // answering him (empty: nothing pending)
        uint32 emote = 0;               // his TEXTEMOTE_*
        uint32 answerAtMs = 0;
        uint32 danceUntilMs = 0;        // != 0: dancing along
        uint32 faceUntilMs = 0;         // != 0: turned to him, the answer then
        uint32 holdUntilMs = 0;         // != 0: answered, standing until then
        bool walkOff = false;           // insulted (timid): the caller moves it on
        bool sighed = false;            // the spam sigh was given
        std::deque<std::pair<ObjectGuid, uint32>> heard;    // who, when
    };

    namespace EmoteReply
    {
        constexpr float HEAR_YD = 25.0f;
        constexpr uint32 SPAM_MS = 30 * IN_MILLISECONDS;
        constexpr uint32 SPAM_COUNT = 3;
        constexpr uint32 ANSWER_MIN_MS = 700, ANSWER_MAX_MS = 1600;
        constexpr uint32 DANCE_PCT = 40;
        constexpr uint32 DANCE_MIN_MS = 5000, DANCE_MAX_MS = 10000;
        constexpr uint32 HOT_PCT = 20, TIMID_PCT = 20;      // of the bots
        constexpr uint32 CALM_WALK_OFF_PCT = 25;            // a calm one insulted walks off too
        // When it may stand (user 2026-11-28: answering mid-stride and walking on at once looked
        // like a machine): it stops, turns to him, meets his eyes this long, answers, and stands
        // a while after.
        constexpr uint32 FACE_MIN_MS = 400, FACE_MAX_MS = 900;
        constexpr uint32 HOLD_MIN_MS = 1500, HOLD_MAX_MS = 3000;

        // Once per bot: its temper (seeded: the same bot stays so).
        void Init(EmoteReplyState& state, uint64 seed);
        // `from` emoted at the bot (PlayerAI::ReceiveEmote).
        void Heard(Player* me, EmoteReplyState& state, Player* from, uint32 textEmote, uint32 now);
        // `canStand`: the bot may stop and stand still a few seconds (turning to him, a dance
        // along, after its answer; its walk is ended). True while it stands for it (the caller
        // keeps still and starts its walk again afterwards).
        bool Update(Player* me, EmoteReplyState& state, bool canStand, uint32 now);
        // Insulted and walking off: true once (the caller sends it elsewhere).
        bool TakeWalkOff(EmoteReplyState& state);
        // Stops a dance (a fight, it leaves).
        void Stop(Player* me, EmoteReplyState& state);
    }
}
}

#endif
