/*
 * Companions, travel: a warlock of the team that waits summons the player back (design
 * .kiro/docs/2026-11-21-companion-talk.md §5, §10; workspace root). Affinity 25
 * (Affinity/Gates WarlockSummon) for a companion; any warlock party bot otherwise.
 *
 *  - Asked: once he is done far away (settled: standing, out of combat, not flying, for
 *    SETTLED_MS), the waiting warlock asks once per departure "shall I summon you?" (no answer:
 *    no). Also at any time from the party unit menu (`.partybot summon [warlock]`, Begin).
 *  - Possible: the warlock knows Ritual of Summoning, has a Soul Shard, is out of a dungeon and a
 *    battleground (the core: a warlock inside can only summon who is inside), nobody of them
 *    fights, he is alive and far (another map or beyond FAR_YD), and two party bots of the group
 *    stand within HELPER_YD of the warlock to click the portal. Not possible: it says why.
 *  - The ritual itself is the team's plan (Commander/Coordination/Situation/Summon.h) and the
 *    bots' hands (PartyBotAI::UpdateSummon).
 *
 * Map thread of the warlock.
 */

#ifndef MANGOS_COMPANIONS_TRAVEL_SUMMON_OFFER_H
#define MANGOS_COMPANIONS_TRAVEL_SUMMON_OFFER_H

#include "Common.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;

namespace Companions
{
namespace SummonOffer
{
    enum class Refusal : uint8
    {
        None,
        NotLearned,     // "notlearned"
        Locked,         // "locked": affinity below the gate
        NoShard,        // "noshard"
        NoHelpers,      // "nohelpers"
        Combat,         // "fighting"
        Here,           // "here": in a dungeon or battleground
        Near,           // "near": he is close already
        Dead,           // "dead"
        Busy,           // "busy": a ritual is under way
    };
    char const* KeyOf(Refusal refusal);

    float constexpr FAR_YD = 100.0f;
    float constexpr HELPER_YD = 30.0f;
    uint32 constexpr SETTLED_MS = 15 * IN_MILLISECONDS;

    // Whether `warlock` can summon `target` now; `helpers` the two who would click.
    Refusal Check(Player* warlock, Player* target, std::vector<ObjectGuid>& helpers);

    // Starts the ritual (Check first). It says so (or why not) in party chat.
    Refusal Begin(Player* warlock, Player* target, std::vector<std::string> const& tags);

    struct State
    {
        uint32 requestId = 0;
        bool asked = false;         // this departure (cleared once he is back)
        uint32 settledSinceMs = 0;
        float lastX = 0.0f, lastY = 0.0f;
        uint32 lastMap = 0;
    };

    // The warlock's question while it waits (`staying`) and he is done far away.
    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags);
}
}

#endif
