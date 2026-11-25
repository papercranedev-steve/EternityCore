/*
 * PartyBot Commander layer 1 (eyes): the walk a tank brings a pack back on, and how safe each
 * point of it is to fight at (user 2026-11-25: bring them back only as far as it takes not to
 * pull more, not to the leader's feet; strat-20261125-233532: a pack 45 yd from the leader's
 * old spot, the fight stood still while each next pack was farther).
 *
 * The navmesh path from where the tank opens to the leader (the way back towards the party),
 * sampled every SAMPLE_STEP. Each point: the add risk there (PullOdds::NearbyAdds) and the
 * first idle creature that would attack someone standing there (SafeRoute::IdleCreatures,
 * IDLE_MARGIN, the pulled creatures left out). Facts only: FightPlace::BringBackPoint chooses.
 *
 * Map thread of the walker (the tank): pathfinds and reads creatures.
 */

#ifndef MANGOS_COMMANDER_BRING_BACK_LINE_H
#define MANGOS_COMMANDER_BRING_BACK_LINE_H

#include "Commander/Perception/Forecast/PullOdds.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Player;

namespace Commander
{
namespace BringBackLine
{
    float const SAMPLE_STEP = 3.0f;

    struct Point
    {
        Position3 pos;
        float yd = 0.0f;                // along the walk from its start
        PullOdds::AddRisk risk;
        ObjectGuid aggroBy;             // an idle creature that would attack there; empty = none
    };

    struct Line
    {
        bool path = false;              // a complete navmesh path (or no navmesh: straight)
        float yd = 0.0f;                // the whole walk
        std::vector<Point> points;      // from the start (yd 0) to the end, both included
    };

    // From `from` (where the tank opens) to `to` (the leader). No complete path: no points.
    Line Along(Player* walker, WorldModel const& world, Position3 const& from, Position3 const& to,
               std::vector<ObjectGuid> const& pulled);
}
}

#endif
