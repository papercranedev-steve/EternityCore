/*
 * PartyBot Commander layer 3: where a ranged bot can stand to attack a unit, from the position
 * field (moved out of RangedStanceBehavior 2026-11-15, so the kite module takes its spot the
 * same way; user: patrols, idle packs and the rest of the field facts for every ranged spot).
 *
 * Find (one search, the order of RangedStanceBehavior.h "Where"):
 *  - cells: FieldSpots::Collect in the band Query::minYd..maxYd (2D from `at`), outside areas
 *    dangerous to ranged (HazardZones); the field drops cells off the ground or cut off from the
 *    party, under an idle creature, on a patrol's way before `staySec`, and cells the bot cannot
 *    walk to. In the fight a circle covering `at` counts only closer than it (FieldSpots.h).
 *    Query::keep: the caller's own rule per cell (the kite module: far enough from the chaser
 *    when the bot gets there).
 *  - loss per cell: SpotCost::Walk + Exposure::LossAt (Query::turnable) + SightMargin::LossAt
 *    (Query::points); cells estimated in sight of `at` first (PositionField::SeesEstimate),
 *    each part by loss, equally good cells shared out (FieldSpots::PreferFree).
 *  - one pass in that order: this bot's own idle scan, paths that failed a moment ago skipped
 *    (SightCache::PathFailed), cells the bot stood on and did not see `at` from
 *    (SightCache::StandFailed), the real line of sight from the sight cache (at most `budget`
 *    new checks), then the spell's line now from the ground of the cell
 *    (SightCache::LiveFromCell) and SafeRoute::PathIsSafe on at
 *    most MAX_GEOMETRY_CHECKS cells; the first that passes is the answer.
 *  - Query::hereValid: the current spot is still good, a cell must save more than
 *    Query::margin on Query::lossHere (the search ends at the first cell that does not, cells
 *    estimated out of sight aside).
 *  - Query::mayTighten: nothing on the preferred line, the hard line next
 *    (SafeRoute::TIGHT_MARGIN, FieldCell::idleTight).
 * Result::why says why no cell was taken (SpotRejects.h); its first idle blocker feeds the join
 * gamble (JoinGamble.h).
 *
 * Map thread of the bot (pathfinding, line of sight). world.field must be set, with its sight
 * cache.
 */

#ifndef MANGOS_COMMANDER_RANGED_SPOT_H
#define MANGOS_COMMANDER_RANGED_SPOT_H

#include "Commander/Behaviors/Movement/SpotRejects.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/StandLoss.h"
#include <functional>
#include <vector>

class Player;

namespace Commander
{
    namespace RangedSpot
    {
        static constexpr float PATH_FACTOR = StandLoss::RANGED_PATH_FACTOR;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 6;    // pathfinding, cells that passed the sight check
        static constexpr float MAX_PATH_DETOUR = 1.8f;

        struct Spot
        {
            Position3 pos;
            float walkSec = 0.0f;
            float exposure = 0.0f;  // points: Exposure::LossAt there
            float sight = 0.0f;     // points: SightMargin::LossAt there
            float loss = 0.0f;      // points: SpotCost::Walk + exposure + sight, lower first
            bool viewed = false;    // PositionField::SeesEstimate from `at`
            bool taken = false;     // FieldSpots::Spot::taken
            bool reached = true;    // FieldSpots::Spot::reached
        };

        struct Query
        {
            Position3 at;               // what the bot must see: its victim, the planned fight spot, a chaser
            float minYd = 0.0f;         // band of cells, 2D from `at`
            float maxYd = 0.0f;
            bool prepare = false;       // before the fight (no fight at `at`: idle circles count fully)
            float staySec = 0.0f;       // no patrol before then (RouteGuard::StaySec)
            std::vector<EnemyFacts const*> const* turnable = nullptr;   // exposure; null = none
            std::vector<Position3> const* points = nullptr;             // sight margin; null = none
            bool unsafe = false;        // the bot stands in an idle creature's reach: the walk starts inside it
            bool mayTighten = false;    // the hard line when nothing on the preferred one
            bool hereValid = false;     // header: a cell must save more than `margin` on `lossHere`
            float lossHere = 0.0f;
            float margin = 0.0f;
            std::function<bool(Spot const&)> keep;     // the caller's own rule per cell; empty = all
        };

        struct Result
        {
            Spot spot;
            bool tight = false;         // taken on the hard line
            size_t cells = 0;           // cells in the band
            uint32 checked = 0;         // cells sight checked
            SpotRejects why;
        };

        // `idle`: this bot's own idle scan (SafeRoute::IdleCreatures(me)); `budget`: sight checks
        // left this tick (used up here). False = no cell (out.why says why).
        bool Find(Player* me, WorldModel const& world, PartyMemberFacts const& self,
                  SafeRoute::IdleCreatures const& idle, Query const& q, uint32& budget, Result& out);
    }
}

#endif
