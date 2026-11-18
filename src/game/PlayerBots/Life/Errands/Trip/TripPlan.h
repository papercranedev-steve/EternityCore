/*
 * Bot daily life (PlayerBots/Life), town trips (Life/Errands/TownTrip.h): what a trip is made of.
 *
 *  - IStopAction: the business at one stop (buy at the auction house, take the mail, sell /
 *    repair / buy at a vendor, learn at the trainer). Runs once the bot stands there.
 *  - TripStop: where (an NPC or a gameobject), what it is called in the question ("place.*"
 *    phrase key, empty: not named), its action, and an optional stop that must come first
 *    (the mailbox after the auction house).
 *  - ITripPlanner: one kind of business (shopping, training). Worth: reason enough to ask the
 *    leader for a trip; PlanStops: its stops for this trip (also when another planner started
 *    it: one trip does all). A new kind of business is a new planner, TownTrip stays as it is.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_PLAN_H
#define MANGOS_LIFE_TRIP_PLAN_H

#include "Common.h"
#include "ObjectGuid.h"
#include <memory>
#include <string>
#include <vector>

class Player;
class WorldObject;

namespace Life
{
namespace Trip
{
    class IStopAction
    {
    public:
        virtual ~IStopAction() = default;
        virtual char const* Name() const = 0;
        // At the stop (in interaction reach of `target`).
        virtual void Do(Player* me, WorldObject* target) = 0;
    };

    struct TripStop
    {
        ObjectGuid target;          // a creature or a gameobject
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::string placeKey;       // Phrases "place.*", empty: not said
        std::shared_ptr<IStopAction> action;
        int after = -1;             // index (in the plan) of a stop that must be visited first
    };

    struct TripPlanStops
    {
        std::vector<TripStop> stops;

        int Add(TripStop stop)
        {
            stops.push_back(std::move(stop));
            return int(stops.size()) - 1;
        }
    };

    class ITripPlanner
    {
    public:
        virtual ~ITripPlanner() = default;
        virtual char const* Name() const = 0;
        virtual bool Worth(Player* me) = 0;
        // Stops near `leader` (Life/Errands/TownTrip::TRIP_YD), appended to `plan`.
        virtual void PlanStops(Player* me, Player const* leader, TripPlanStops& plan) = 0;
    };
}
}

#endif
