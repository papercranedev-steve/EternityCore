/*
 * PartyBot Commander layer 2 (brain, area phase): where a ground channel (Blizzard, Rain of
 * Fire) goes and where it is cast from, by the user's two rules (doc
 * .kiro/docs/2026-11-12-mage-hold.md 3.2, 3.3). AreaPhase asks it for each candidate; until
 * 2026-11-12 the centre was always an enemy's place.
 *
 * OnPile  nothing runs at the caster: the cast spot is AreaSpot's for the area around
 *         `anchor`; from that spot the centre moves to the near side of what it covers
 *         (AreaCentre::NearSide: the farthest one at the far rim) when that is worth more
 *         (AreaValue). The spots that would leave the moved centre out of range are dropped.
 * OnLead  enemies run at the caster (Runners, `lead` the first): the centre ahead of the lead
 *         (AreaCentre::AheadOf), cast from where the caster stands (no walk with an enemy on the
 *         way). Not when the ticks before the lead is on the caster (Runners::ArriveSec -
 *         AreaCentre::CAST_DELAY_SEC) do not pay (AreaValue on those ticks, its whole mana):
 *         the caster gets room first (doc 3.3.1, 3.4; later steps).
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_AREA_PLACE_H
#define MANGOS_COMMANDER_AREA_PLACE_H

#include "Commander/Perception/Values/AreaValue.h"
#include <vector>

namespace Commander
{
    struct TeamPlan;

    namespace AreaPlace
    {
        struct Choice
        {
            bool valid = false;
            AreaValue::Request request;     // centre and from set
            AreaValue::Result value;        // the whole spell from request.from, walk not in it
            float netHere = 0.0f;           // cast from where the caster stands (0: not possible)
            bool move = false;              // walk to spots first
            std::vector<Position3> spots;   // best first (AreaSpot)
            float walkCost = 0.0f;
            float gain = 0.0f;              // AreaSpot::Choice::gain
        };

        // `base`: caster, spell (a ground channel), turn options, forbidden; centre and from
        // are set here. `prevSpot`: AreaSpot's spot of the last refresh (nullptr: none).
        Choice OnPile(WorldModel const& world, TeamPlan const& plan, AreaValue::Request const& base,
                      EnemyFacts const& anchor, Position3 const* prevSpot);

        Choice OnLead(WorldModel const& world, AreaValue::Request const& base, EnemyFacts const& lead);
    }
}

#endif
