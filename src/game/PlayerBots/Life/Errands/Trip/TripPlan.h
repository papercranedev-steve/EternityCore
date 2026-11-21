/*
 * Bot daily life (PlayerBots/Life), town trips (Life/Errands/TownTrip.h): what a trip is made of.
 *
 *  - IStopAction: the business at one stop (buy at the auction house, take the mail, sell /
 *    repair / buy at a vendor, learn at the trainer). Runs when the bot gets there; then it
 *    stands its BrowseTime, longer the more it handled.
 *  - TripStop: where (an NPC or a gameobject), what it is called in the question ("place.*"
 *    phrase key, empty: not named), its action, and an optional stop that must come first
 *    (the mailbox after the auction house).
 *  - ITripPlanner: one kind of business (shopping, training). Worth: reason enough to ask the
 *    leader for a trip; PlanStops: its stops for this trip (also when another planner started
 *    it: one trip does all). Only the planners with a stop left after the route is ordered
 *    are in the question (their purpose, notes and shortfall). A new kind of business is a
 *    new planner, TownTrip stays as it is.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_PLAN_H
#define MANGOS_LIFE_TRIP_PLAN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Requests/RequestBoard.h"
#include <memory>
#include <string>
#include <vector>

class Player;
class WorldObject;

namespace Life
{
namespace Trip
{
    // How long the bot stands at a stop (a player browses the window too): a look around
    // (random in [baseMinMs, baseMaxMs]) and then, for every thing handled (Do's count),
    // perThingMs at 70–130 %. At most capMs.
    struct Browse
    {
        uint32 baseMinMs;
        uint32 baseMaxMs;
        uint32 perThingMs;
        uint32 capMs;
    };

    class IStopAction
    {
    public:
        virtual ~IStopAction() = default;
        virtual char const* Name() const = 0;
        // Default: a vendor or a trainer, looking through the list.
        virtual Browse BrowseTime() const
        {
            return { 4 * IN_MILLISECONDS, 8 * IN_MILLISECONDS, 2 * IN_MILLISECONDS, 40 * IN_MILLISECONDS };
        }
        // At the stop (in interaction reach of `target`), as soon as it arrives; it stands its
        // BrowseTime afterwards. Returns how many things it handled (listings bought, stacks
        // sold, mails taken, spells learned).
        virtual uint32 Do(Player* me, WorldObject* target) = 0;
    };

    struct TripStop
    {
        ObjectGuid target;          // a creature or a gameobject
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::string placeKey;       // Phrases "place.*", empty: not said
        std::shared_ptr<IStopAction> action;
        int after = -1;             // index (in the plan) of a stop that must be visited first
        // From the spawn table, maybe across the city (a capital's class trainer): its grid,
        // and so its navmesh, may not be loaded yet. A walk that cannot be measured is no
        // reason to drop it (Trip/RouteOrder: ordered by the straight distance); the bot walks
        // towards the spawn point and the grid loads on the way (TownTrip).
        bool farSpawn = false;
        int planner = -1;           // TownTrip: index of the planner that added it
        // After every stop that is not `last` (Trip/RouteOrder): the lowest business (profession
        // trainers) spends what the others left.
        bool last = false;
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
        // Stops near `leader` (Life/Errands/TownTrip::TRIP_YD; a planner may look further),
        // appended to `plan`.
        virtual void PlanStops(Player* me, Player const* leader, TripPlanStops& plan) = 0;
        // Lines the question shows under its text in the popup (what the business is about),
        // in `reader`'s client language. Default: none.
        virtual void Notes(Player* /*me*/, Player const* /*reader*/, std::vector<Requests::Ask::Note>& /*out*/) {}
        // Copper the bot lacks for this business: the question then offers to give it
        // (TownTrip: "pay"). Default: none.
        virtual uint32 Shortfall(Player* /*me*/) { return 0; }
        // What the business is for, said in the question (Phrases key "errand.why.*", e.g.
        // "learn new spells"). nullptr: not said.
        virtual char const* PurposeKey() const { return nullptr; }
    };
}
}

#endif
