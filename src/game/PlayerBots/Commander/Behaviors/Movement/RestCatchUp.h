/*
 * PartyBot Commander layer 3 (cerebellum): a resting bot whose leader walks away stands up and
 * catches up, then sits again (user, 2026-11-21: a bot that sat down to drink sat to full while
 * the player walked off; a companion is not teleported after him, it was left behind).
 *
 * A bot runs as fast as the leader: while he keeps walking it only keeps the gap, it catches up
 * once he stops. So the question is whether he waits or leaves, the distance comes second:
 *   - leaving: farther than LEAVING_YD and moving (looting a corpse, a step to a quest giver
 *     stay within it: about 4 s of running at 7 yd/s, near the casters' 30 yd range), or
 *     farther than FAR_YD whatever he does (he stopped to fight out of its reach);
 *   - then it stands up and does not sit down again (TeamBotAI::HoldsOffRest, RestBehavior)
 *     until it is within BACK_YD of him and he has stood still for STILL_MS (no sitting down
 *     and standing up again while he walks a few steps at a time);
 *   - a rest that ends within FINISH_SEC is finished first (the leader is not far yet).
 * Sitting down again starts below RestValue::START_SHARE only (each sitting uses a new food
 * or drink).
 *
 * Only the free rest (no fight on or planned): the caller does not run it while the Commander
 * has an out-of-combat plan, a bot waits at its stay spot or a resurrection comes first; with
 * no one to follow (the guide), in combat or on another map it ends.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_REST_CATCH_UP_H
#define MANGOS_COMMANDER_REST_CATCH_UP_H

#include "Common.h"

class TeamBotAI;
class Unit;

namespace Commander
{
    // Per bot (TeamBotAI member). Plain data.
    struct RestCatchUpState
    {
        bool active = false;        // caught up yet? (no rest until then)
        uint32 stillSinceMs = 0;    // the leader stands still since (0: moving)
    };

    namespace RestCatchUp
    {
        constexpr float LEAVING_YD = 30.0f;
        constexpr float FAR_YD = 45.0f;
        constexpr float BACK_YD = 10.0f;
        constexpr uint32 STILL_MS = 2500;
        constexpr float FINISH_SEC = 3.0f;

        // Every out-of-combat bot update; `leader`: who it follows, null when this does not
        // apply now (ends it). True: catching up (no resting, a resting bot stands up).
        bool Update(TeamBotAI const* bot, RestCatchUpState& state, Unit const* leader);
    }
}

#endif
