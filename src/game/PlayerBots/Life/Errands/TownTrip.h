/*
 * Bot daily life (PlayerBots/Life), errand: "wait for me, I go to the vendor and the trainer".
 * Soul companions doc §5 (stage S5).
 *
 * NpcVisit.h does the business with NPCs right by the party. This one walks out of the party
 * to NPCs further away (up to TRIP_YD from the leader), with the leader's consent. The leader
 * is free meanwhile (his own trainer, the bank): the bot does not follow, and companions are
 * not teleported to a far leader on the same map (PartyBotAI::CatchesUpByTeleport).
 *   Idle      every CHECK_MS: the leader is in a rest area (a city or an inn) and standing, the
 *             bot may run errands (ErrandCommon::MayRun) and some planner (Trip/TripPlan.h:
 *             shopping, training) finds it worth a trip. Then every planner adds its stops
 *             (one trip does all), the route is ordered by real walking distance
 *             (Trip/RouteOrder.h) and one question names every place (doc §5: merged).
 *   Asking    the bot says the question in party chat (Phrases "errand.ask") and opens a
 *             request (Requests::Board, intent Errand). It keeps following meanwhile. "yes" or
 *             no answer (the fallback: a player who ignores the popup does not mind): it goes.
 *             "no": it stays and the intent backs off.
 *   Going     the stops in turn: walk there (Trip/Approach.h), stand DWELL_MS, then the stop's action.
 * Done, the leader within NEAR_YD:
 *   Returning back to him (BACK_YD).
 * Done, the leader farther (busy elsewhere in town): it whispers him and asks (intent
 * Rendezvous, "meet.ask"): "come" (to him: the follow walks it there), "gate" (the nearest
 * recorded town gate, Shared/TownGates; "here" when the town has none). No answer: "come".
 *   Reporting the question is open; the bot stands where it is.
 *   Coming    it follows again (not busy) until within BACK_YD of him, then says it is back.
 *   ToGate    walks to the gate,
 *   Waiting   stands there (whispers once it arrived) until the leader comes within MEET_YD.
 * After a trip: COOLDOWN_MS before the next. Ends at once (request cancelled) when the bot may
 * no longer run errands (combat, mounted ...) or the leader changes map.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TOWN_TRIP_H
#define MANGOS_LIFE_TOWN_TRIP_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/Trip/Approach.h"
#include "Life/Errands/Trip/TripPlan.h"
#include <string>
#include <vector>

class Player;

namespace Life
{
    // Per bot (a member of its AI).
    struct TownTripState
    {
        enum class Phase : uint8
        {
            Idle,
            Asking,
            Going,
            Returning,
            Reporting,
            Coming,
            ToGate,
            Waiting,
        };

        Phase phase = Phase::Idle;
        uint32 requestId = 0;
        std::vector<Trip::TripStop> stops;
        size_t next = 0;
        uint32 nextCheckMs = 0;
        uint32 phaseSinceMs = 0;    // the current phase or stop since
        uint32 nextMoveMs = 0;
        Trip::ApproachState leg;    // to the current stop
        uint32 cooldownUntilMs = 0;
        uint32 leaderMap = 0;       // the leader's map when the trip began
        // Where it waits (a town gate, or where it finished) and what it calls the place.
        float waitX = 0.0f, waitY = 0.0f, waitZ = 0.0f;
        std::string waitName;
        bool hasGate = false;
    };

    namespace TownTrip
    {
        constexpr float TRIP_YD = 150.0f;
        constexpr float NEAR_YD = 40.0f;        // done: just walk back, no question
        constexpr float BACK_YD = 8.0f;
        constexpr float MEET_YD = 20.0f;        // waiting: the leader is here
        constexpr float AT_GATE_YD = 3.0f;
        constexpr float MAX_PATH_DETOUR = 3.0f;
        constexpr uint32 CHECK_MS = 10 * IN_MILLISECONDS;
        constexpr uint32 STOP_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        constexpr uint32 RETURN_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        constexpr uint32 COME_GIVE_UP_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 GATE_GIVE_UP_MS = 3 * MINUTE * IN_MILLISECONDS;    // the walk there
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 COOLDOWN_MS = 15 * MINUTE * IN_MILLISECONDS;

        // `planners`: the kinds of business; `tags` the bot's personality (what it says).
        // True: the bot is on its trip this tick (it does not follow).
        bool Update(Player* me, TownTripState& state, Player* leader,
                    std::vector<Trip::ITripPlanner*> const& planners, std::vector<std::string> const& tags);
        // Cancels the question or the trip (combat, a Commander plan).
        void Stop(Player* me, TownTripState& state);
    }
}

#endif
