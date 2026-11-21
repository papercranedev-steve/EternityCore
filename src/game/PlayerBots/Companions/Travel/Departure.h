/*
 * Companions, travel: the player leaves the team by hearthstone (or any spell that teleports
 * himself: a mage's Teleport, Astral Recall) or a flight path, and one of the party asks "shall
 * we wait here?" (design .kiro/docs/2026-11-21-companion-talk.md §4; agent suggestion written
 * into the doc: only those two ways of leaving, the whole party stays).
 *
 * One bot of the party asks (the first party bot of the group next to him), once per departure
 * (again only after he was back with it). "Wait here" (or no answer) puts every party bot of the
 * group into its stay (Commander/Coordination/Situation/Stay.h); "follow" leaves them as they
 * are. Not while any of them already waits, nor when he hearths and some of them go home with
 * him (Companions/Travel/HearthAlong; the others follow on their own, Life/Errands/Travel).
 *
 * Map thread of the bot (each bot runs it; only the asker does anything).
 */

#ifndef MANGOS_COMPANIONS_TRAVEL_DEPARTURE_H
#define MANGOS_COMPANIONS_TRAVEL_DEPARTURE_H

#include "Common.h"
#include <string>
#include <vector>

class Player;

namespace Companions
{
namespace Departure
{
    struct State
    {
        uint32 requestId = 0;
        bool asked = false;     // asked for this departure (cleared once he is back with it)
    };

    float constexpr NEAR_YD = 50.0f;    // "with the team": the asker is this close to him

    // `staying`: this bot waits already (then nobody is asked).
    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags);
}
}

#endif
