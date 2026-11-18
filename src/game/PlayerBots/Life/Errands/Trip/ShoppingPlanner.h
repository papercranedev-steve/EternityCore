/*
 * Bot daily life (PlayerBots/Life), town trips: shopping (soul companions doc §4.4, §5).
 *
 * Worth a trip: a critical supply is missing (Shared/Supplies), an equipped item is nearly
 * broken (TRIP_DURABILITY_PCT), or the bags are full with junk to sell.
 *
 * Plan, one money and bag budget for the whole trip (Shared/Purchasing: critical needs may
 * spend everything, the rest keeps the class trainer's price and two free slots), tier by
 * tier (Critical, then Important, then Optional):
 *  1. the auction house first (cheaper, and it has most reagents, food and drink;
 *     Shared/AuctionMarket: never dearer than a vendor), at the auctioneer nearest the leader
 *     with a mailbox nearby; bought items come by mail, so the mailbox stop follows it;
 *  2. what is still missing at vendors: first those already on the route, then new ones
 *     picked greedily, each time the vendor that covers the most (critical weighs most,
 *     repairs count when it is an armorer), ties to the one nearer the leader; at most
 *     MAX_VENDORS vendors. Junk is sold at the first vendor of the route.
 * Each stop keeps what it was planned to buy (goods and units): at the stop it buys that much
 * with live prices, so two stops never buy the same need twice (the auction house items are
 * still in the mail when the vendors come). Mail waiting already: a mailbox stop too.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_SHOPPING_PLANNER_H
#define MANGOS_LIFE_TRIP_SHOPPING_PLANNER_H

#include "Life/Errands/Trip/TripPlan.h"

namespace Life
{
namespace Trip
{
    class ShoppingPlanner final : public ITripPlanner
    {
    public:
        static constexpr uint32 TRIP_DURABILITY_PCT = 25;
        static constexpr size_t MAX_VENDORS = 4;
        static constexpr float MAILBOX_NEAR_AUCTION_YD = 60.0f;

        char const* Name() const override { return "shopping"; }
        bool Worth(Player* me) override;
        void PlanStops(Player* me, Player const* leader, TripPlanStops& plan) override;
    };
}
}

#endif
