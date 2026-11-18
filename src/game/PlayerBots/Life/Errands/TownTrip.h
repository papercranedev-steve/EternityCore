/*
 * Bot daily life (PlayerBots/Life), errand: "wait for me, I go to the vendor and the trainer".
 * Soul companions doc §5 (stage S5).
 *
 * NpcVisit.h does the business with NPCs right by the party. This one walks out of the party
 * to NPCs further away (up to TRIP_YD from the leader), with the leader's consent:
 *   Idle      every CHECK_MS: the leader is in a rest area (a city or an inn) and standing, the
 *             bot may run errands (ErrandCommon::MayRun), a task is worth a trip (INpcTask::
 *             WorthTrip) and has an NPC (Shared/NpcDirectory, the live creature: Wants, safe
 *             walk) further than NpcVisit::ANCHOR_YD from the leader. All tasks' stops make one
 *             trip and one question (doc §5: merged).
 *   Asking    the bot says the question in party chat (Phrases "errand.ask") and opens a
 *             request (Requests::Board, intent Errand). It keeps following meanwhile. "yes" or
 *             no answer (the fallback: a player who ignores the popup does not mind): it goes.
 *             "no": it stays and the intent backs off.
 *   Going     the stops in turn, each like NpcVisit (stand DWELL_MS, then the task).
 *   Returning back to the leader (within BACK_YD), then COOLDOWN_MS before the next trip.
 * Aborted (request cancelled, walk stopped) when the bot may no longer run errands, the leader
 * mounts, changes map or walks LEADER_LEAVE_YD away from where he stood: the follow, the
 * arrival walk and the far-teleport of PartyBotAI bring it back.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TOWN_TRIP_H
#define MANGOS_LIFE_TOWN_TRIP_H

#include "Common.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;

namespace Life
{
    class INpcTask;

    struct TownTripStop
    {
        ObjectGuid npc;
        INpcTask* task = nullptr;
    };

    // Per bot (a member of its AI).
    struct TownTripState
    {
        enum class Phase : uint8
        {
            Idle,
            Asking,
            Going,
            Returning,
        };

        Phase phase = Phase::Idle;
        uint32 requestId = 0;
        std::vector<TownTripStop> stops;
        size_t next = 0;
        uint32 nextCheckMs = 0;
        uint32 phaseSinceMs = 0;    // the current phase or stop since
        uint32 nextMoveMs = 0;
        uint32 arrivedMs = 0;       // standing at the current stop since, 0 = not there
        uint32 cooldownUntilMs = 0;
        uint32 leaderMap = 0;
        float leaderX = 0.0f, leaderY = 0.0f;   // where the leader stood when it left
    };

    namespace TownTrip
    {
        constexpr float TRIP_YD = 150.0f;
        constexpr float LEADER_LEAVE_YD = 30.0f;
        constexpr float BACK_YD = 8.0f;
        constexpr float MAX_PATH_DETOUR = 3.0f;
        constexpr uint32 CHECK_MS = 10 * IN_MILLISECONDS;
        constexpr uint32 STOP_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        constexpr uint32 RETURN_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 COOLDOWN_MS = 15 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 ABORT_COOLDOWN_MS = 5 * MINUTE * IN_MILLISECONDS;   // the party walked off

        // `tasks` in visiting order; `tags` the bot's personality (what it says).
        // True: the bot is out on its trip this tick (it does not follow).
        bool Update(Player* me, TownTripState& state, Player* leader,
                    std::vector<INpcTask*> const& tasks, std::vector<std::string> const& tags);
        // Cancels the question or the trip (combat, a Commander plan).
        void Stop(Player* me, TownTripState& state);
    }
}

#endif
