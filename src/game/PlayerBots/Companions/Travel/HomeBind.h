/*
 * Companions, travel: when the player makes an inn his home (his hearthstone bound there), the
 * companions with him do the same, as friends travelling together would (design doc
 * .kiro/docs/2026-11-21-companion-presence.md §6, user 2026-11-21). Their home is then where
 * they hearth to (Life/Errands/Travel, WalkAway) and where they go back to after 4 hours away
 * (CompanionMgr::SendHome).
 *
 * Seen as a change of his bind point while this bot is within WITH_YD of him (the innkeeper
 * talk itself is not watched); one line in party chat. Not a bot that waits elsewhere.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMPANIONS_TRAVEL_HOME_BIND_H
#define MANGOS_COMPANIONS_TRAVEL_HOME_BIND_H

#include "Common.h"
#include "SharedDefines.h"
#include <string>
#include <vector>

class Player;

namespace Companions
{
namespace HomeBind
{
    struct State
    {
        bool known = false;
        WorldLocation seen;     // his bind point last time
    };

    float constexpr WITH_YD = 30.0f;
    float constexpr SAME_YD = 5.0f;

    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags);
}
}

#endif
