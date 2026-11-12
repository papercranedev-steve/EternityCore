/*
 * PartyBot Commander layer 2 (brain, area phase): where the area caster casts a ground channel
 * from (.kiro/docs/2026-11-11-aoe-threat.md 4.5, user 2026-11-12: stand there before the spell,
 * not once an enemy is predicted to turn, that is too late; not always the farthest: a spot that
 * would pull more is out). Replaces the one point on the line centre -> caster (2026-11-08).
 *
 * Candidates: the position field's cells (FieldSpots::Collect: no idle creature's reach, no
 * patrol before RouteGuard::StaySec, reachable) with the centre within range - SPOT_MARGIN_YD,
 * out of ranged hazard zones; and where the caster stands, when the centre is in range and
 * its cell is clear of idle creatures. Score of a cell:
 *   net    AreaValue's net of the whole spell cast from it (`evaluate`; before the fight: 0)
 *   slip   Exposure::ChannelLoss of the enemy most likely to turn (in case it slips): of the
 *          spell's targets on a party member, the one with the shortest threat runway with
 *          this spell's threat (ThreatRunway); none: an enemy at the centre. Its speed under
 *          the spell's slow.
 *   walk   SpotCost::Walk (grid walk x PATH_FACTOR at RUN_YD_PER_SEC)
 *   score  = net - slip - walk; a cell within one cell of the previous spot + MOVE_MARGIN.
 * The cells are ranked by slip + walk first; AreaValue values the best MAX_EVALUATED. The
 * caster moves when the best cell beats standing here by more than SpotCost::Walk(MOVE_MARGIN_SEC).
 * The best MAX_SPOTS cells are handed on: the cerebellum checks sight and path (live) in that order.
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_AREA_SPOT_H
#define MANGOS_COMMANDER_AREA_SPOT_H

#include "Commander/Perception/Values/AreaValue.h"
#include <vector>

namespace Commander
{
    struct TeamPlan;

    namespace AreaSpot
    {
        static constexpr float SPOT_MARGIN_YD = 3.0f;
        static constexpr float RUN_YD_PER_SEC = 7.0f;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr float MOVE_MARGIN_SEC = 1.0f;
        static constexpr uint32 MAX_EVALUATED = 3;
        static constexpr uint32 MAX_SPOTS = 3;

        struct Choice
        {
            bool valid = false;         // here or a cell
            bool move = false;          // a cell beats here
            AreaValue::Result value;    // from the chosen place (`evaluate`), walk not in it
            float walkCost = 0.0f;
            float gain = 0.0f;          // score there - score here (walk included); move only
            float hereNet = 0.0f;       // AreaValue's net here (0: out of range / not evaluated)
            bool hereValid = false;
            std::vector<Position3> spots;   // move: best first, at most MAX_SPOTS
        };

        // `base`: caster, spell (a ground channel), centre; `from` is ignored. `prevSpot`: the
        // spot of the last refresh (nullptr: none).
        Choice Pick(WorldModel const& world, TeamPlan const& plan, AreaValue::Request const& base, bool evaluate,
                    Position3 const* prevSpot);
    }
}

#endif
