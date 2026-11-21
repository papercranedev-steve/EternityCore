/*
 * PartyBot Commander layer 2 (situation): the team gathers its members left behind
 * (Perception/Spatial/LeftBehind: a bot that logged in where it logged out, one far behind on
 * the road) by its warlocks' summons. Design .kiro/docs/2026-11-21-companion-talk.md §11
 * (decided with the user 2026-11-21). Any team size: in a 40 man raid every warlock with the
 * team summons one at a time, several at once, two helpers each, nobody in two rituals.
 *
 *  - When: the team stands still out of a fight (the leader settled for SETTLED_MS: not moving,
 *    not flying, no enemy of the party), outside dungeons and battlegrounds (a warlock inside
 *    can only summon who is inside: gather at the entrance). The player's "把掉队的都拉过来"
 *    (`.partybot summon all`, TeamPlan::summon.gatherNow) skips the wait.
 *  - Who: bots only, not asked (a warlock says "我去把 X 拉过来"); the tank first, then
 *    healers, then the others (LeftBehind's order). The player gets his own question
 *    (Companions/Travel/SummonOffer). No affinity gate: it is the bot playing well.
 *  - Shards: a warlock keeps one (Summon::SHARDS_KEPT_FOR_TEAMMATES) for a Soulstone; none to
 *    spare: one line how many are still missing, once per settle.
 *  - Nobody can: nothing; the old catch up by teleport still brings them (for now).
 *
 * Map thread of the team, from PartyCommander::Update.
 */

#ifndef MANGOS_COMMANDER_GATHER_PLANNER_H
#define MANGOS_COMMANDER_GATHER_PLANNER_H

#include "Common.h"

class TeamBotAI;

namespace Commander
{
    class PartyCommander;

    namespace GatherPlanner
    {
        uint32 constexpr SETTLED_MS = 15 * IN_MILLISECONDS;
        float constexpr WITH_TEAM_YD = 40.0f;   // a warlock with the team: this close to the leader
        float constexpr HELPER_YD = 30.0f;      // helpers this close to their warlock
        uint32 constexpr RITUAL_GIVE_UP_MS = 4 * MINUTE * IN_MILLISECONDS;     // a ritual nobody ended

        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
