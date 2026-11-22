/*
 * Bot daily life (PlayerBots/Life), pastimes: admiring a real player's gear (population
 * interactions, user 2026-11-22: "rich young master returns to the village").
 *
 * What catches the eye is Shared/ShowyGear (per visible piece, against the player's own level).
 * A bot free to look around rolls once per player per SAME_PLAYER_MS when he comes within
 * ADMIRE_YD in sight: chance = CHANCE_PER_POINT x the things' points, each thing's points halved
 * for every time the player was admired with it on (habituation: new gear is looked at again,
 * the same old set less and less), at most CHANCE_MAX. All bots share each player's attention
 * budget (a token bucket: one admiring per TOKEN_REFILL_MS, at most TOKENS_MAX saved), so a
 * town gives a newcomer a look or two, then one now and then.
 *
 * How it admires depends on the bot's level against the player's:
 *   - far below (NOVICE_BELOW levels or more): stares, gasps, drools, bows;
 *   - about the same: whistles, applauds, claps, points him out;
 *   - above him: a nod, a clap.
 * Now and then (SAY_PCT; chatty ones more, quiet ones less) it says a line about the piece
 * (admire.say.* in PlayerBots/i18n). A talking group turns to him together (ChatCircle watch).
 * The emote, and the line after it, only once he looks at it (Shared/Gaze); he never does
 * while it watches him: neither.
 *
 * Map thread of the bot (the attention budget is shared by all bots: guarded).
 */

#ifndef MANGOS_LIFE_PASTIMES_ADMIRE_H
#define MANGOS_LIFE_PASTIMES_ADMIRE_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Shared/ShowyGear.h"
#include <deque>
#include <string>
#include <utility>
#include <vector>

class Player;

namespace Life
{
namespace Pastimes
{
    enum class AdmireStance : uint8 { Novice, Peer, Senior };

    struct AdmireState
    {
        ObjectGuid looking;             // admiring him now (empty: nobody)
        uint32 untilMs = 0;
        uint32 actAtMs = 0;             // != 0: the emote then
        uint32 sayAtMs = 0;             // != 0: the line then
        AdmireStance stance = AdmireStance::Peer;
        BotShared::ShowyGear::Look look = BotShared::ShowyGear::Look::Weapon;
        uint32 nextCheckMs = 0;
        std::deque<std::pair<ObjectGuid, uint32>> judged;   // players it rolled for, when
    };

    namespace Admire
    {
        constexpr float ADMIRE_YD = 20.0f;
        constexpr uint32 CHECK_MS = 2000;
        constexpr uint32 LOOK_MIN_MS = 4000, LOOK_MAX_MS = 7000;
        constexpr uint32 ACT_MIN_MS = 800, ACT_MAX_MS = 1800;   // the emote, once he looks
        constexpr uint32 SAY_AFTER_MIN_MS = 1200, SAY_AFTER_MAX_MS = 2000;
        constexpr uint32 HOLD_AFTER_MS = 1500;                  // still looking after the last of it
        constexpr uint32 SAY_PCT = 15, SAY_CHATTY_PCT = 30, SAY_QUIET_PCT = 5;
        constexpr uint32 SAME_PLAYER_MS = 15 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 TOKEN_REFILL_MS = 4 * MINUTE * IN_MILLISECONDS;
        constexpr float TOKENS_MAX = 2.0f;
        constexpr float CHANCE_PER_POINT = 0.2f;
        constexpr float CHANCE_MAX = 0.8f;
        constexpr uint32 NOVICE_BELOW = 8;

        // `free`: it may look around now (a new admiring may start). True while admiring (the
        // caller keeps still). `tags`: its personality (the line, how chatty).
        bool Update(Player* me, AdmireState& state, bool free, uint32 now, std::vector<std::string> const& tags);
        void Stop(AdmireState& state);
        // Whom it admires now (empty: nobody).
        inline ObjectGuid Watching(AdmireState const& state) { return state.looking; }
    }
}
}

#endif
