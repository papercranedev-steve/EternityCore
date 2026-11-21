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
 *             (Trip/RouteOrder.h) and one question names every place and what for (doc §5:
 *             merged; shopping and the trainer on one trip, user 2026-11-20).
 *   Asking    the bot says the question in party chat (Phrases "trip.ask") and opens a
 *             request (Requests::Board, intent Errand); the popup also lists the notes of the
 *             planners with a stop left on the route (the lessons by tier, what they cost).
 *             It keeps following meanwhile and may sit down (drinks, sits by the idle
 *             leader): the question stays open; the answer to go stands it up. "yes"
 *             or no answer (the fallback: a player who ignores the popup does not mind): it
 *             goes. "no": it stays and the intent backs off. Short of money for the business
 *             (ITripPlanner::Shortfall): a third answer, "pay" (user 2026-11-20).
 *   Collecting "pay": it walks up to the leader and opens a trade for his money; whatever he
 *             gives (nothing, the window closed: as "yes"), it goes. A trade that does not
 *             work is tried again MAX_TRADE_RETRIES times.
 *   Going     the stops in turn: walk there (Trip/Approach.h; an NPC not loaded yet: to its
 *             spawn point first), the stop's action, then stand its BrowseTime (longer the
 *             more it bought / sold / learned).
 * Done, the leader within NEAR_YD:
 *   Returning back to him (BACK_YD).
 * Done, the leader farther (busy elsewhere in town): it whispers him and asks (intent
 * Rendezvous, "meet.ask"): "come" (to him: the follow walks it there), "gate" (the nearest
 * recorded town gate, Shared/TownGates; "here" when the town has none). No answer: "come".
 *   Reporting the question is open; the bot stands where it is.
 *   Coming    walks to him itself (pathfinding, repathed as he moves) until within BACK_YD,
 *             then says it is back; after COME_GIVE_UP_MS the far travel may take over
 *             (Life/Errands/Travel, via PartyBotAI::UpdateTravel).
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

namespace BotShared { namespace Trade { struct Hands; } }

namespace Life
{
    // Per bot (a member of its AI).
    struct TownTripState
    {
        enum class Phase : uint8
        {
            Idle,
            Asking,
            Collecting,     // "pay": takes the leader's money in a trade before it goes
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
        uint32 browseMs = 0;        // how long it stands at the current stop (0: business not done yet)
        // The current stop's give-up time, decided when the stop begins (0: not yet). Decided
        // once: a far stop's 3 minutes must not turn into 60 s when its NPC loads on the way.
        uint32 stopGiveUpMs = 0;
        uint32 cooldownUntilMs = 0;
        uint32 leaderMap = 0;       // the leader's map when the trip began
        // Where it waits (a town gate, or where it finished) and what it calls the place.
        float waitX = 0.0f, waitY = 0.0f, waitZ = 0.0f;
        std::string waitName;
        bool hasGate = false;
        // Collecting: the trade with the leader (Shared/Trade).
        bool tradeBegun = false;
        uint32 tradeFailures = 0;
        uint32 moneyBefore = 0;
        // What the leader gave in that trade, until TakePaid reads it.
        uint32 paidCopper = 0;
    };

    namespace TownTrip
    {
        constexpr float TRIP_YD = 150.0f;
        // Done and the leader within this: just walk back, no question. A question only pays
        // when he went elsewhere in town (the mage quarter while the bot was at the bank).
        constexpr float NEAR_YD = 150.0f;
        constexpr float BACK_YD = 8.0f;
        constexpr float MEET_YD = 20.0f;        // waiting: the leader is here
        constexpr float AT_GATE_YD = 3.0f;
        constexpr float MAX_PATH_DETOUR = 3.0f;
        constexpr uint32 CHECK_MS = 10 * IN_MILLISECONDS;
        constexpr uint32 STOP_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        // A stop farther than TRIP_YD (a capital's trainer across the city).
        constexpr uint32 FAR_STOP_GIVE_UP_MS = 3 * MINUTE * IN_MILLISECONDS;
        // Its spawn point is walked to until the NPC itself is there (its grid loads on the way).
        constexpr float SPAWN_REACH_YD = 5.0f;
        // Collecting the leader's money.
        constexpr float TRADE_YD = 3.0f;
        constexpr uint32 COLLECT_GIVE_UP_MS = 2 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 MAX_TRADE_RETRIES = 2;
        constexpr uint32 RETURN_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        constexpr uint32 COME_GIVE_UP_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 GATE_GIVE_UP_MS = 3 * MINUTE * IN_MILLISECONDS;    // the walk there
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 COOLDOWN_MS = 15 * MINUTE * IN_MILLISECONDS;

        // `planners`: the kinds of business; `tags` the bot's personality (what it says);
        // `hands`: the bot's trade (Collecting).
        // True: the bot is on its trip this tick (it does not follow).
        bool Update(Player* me, TownTripState& state, Player* leader,
                    std::vector<Trip::ITripPlanner*> const& planners, std::vector<std::string> const& tags,
                    BotShared::Trade::Hands& hands);
        // Cancels the question or the trip (combat, a Commander plan).
        void Stop(Player* me, TownTripState& state, BotShared::Trade::Hands& hands);
        // The money the leader gave when it asked ("pay"), once; 0 = none since the last call.
        uint32 TakePaid(TownTripState& state);
    }
}

#endif
