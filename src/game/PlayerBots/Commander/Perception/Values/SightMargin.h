/*
 * PartyBot Commander layer 1 (eyes): what a spot that sees the target only narrowly costs a
 * member who must keep it in sight (a ranged dealer its victim, a healer the tank), in points
 * (2026-11-14, stockade-20261114-224312: the mage and the priest stood in the doorway of the
 * prisoners' cell, saw the pile's front, and lost it whenever an enemy moved further in; the
 * ranged stance then found nothing for 4 s).
 *
 * The target moves within the fight: an enemy to the others of its pile and to the member it
 * hits, the tank among the enemies it holds. Points(target) are those places: the acting
 * enemies fighting us within PILE_YD of the target (not the target itself, not within one cell
 * of it), plus an enemy target's victim (not the member itself); nearest MAX_POINTS.
 *
 *   LossAt(p) = MOVE_CHANCE x hidden / points x Value::LostAction(member, REACQUIRE_SEC)
 *
 * hidden = points `p` does not see by the position field's estimate (PositionField::
 * SeesEstimate, 2026-11-15: a soft score for every candidate cell, no vmap call).
 * REACQUIRE_SEC: the target gone out of sight, the stance notices on its next tick and walks
 * to a cell that sees it again (the spell in progress is lost too). No points (a lone enemy): 0.
 * Facts (positions) and the field; map thread of the party.
 */

#ifndef MANGOS_COMMANDER_SIGHT_MARGIN_H
#define MANGOS_COMMANDER_SIGHT_MARGIN_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    class PositionField;

namespace SightMargin
{
    float const MOVE_CHANCE = 0.5f;         // the target moves to one of the points within the horizon
    float const REACQUIRE_SEC = 3.0f;
    float const PILE_YD = 10.0f;
    uint32 const MAX_POINTS = 4;

    std::vector<Position3> Points(WorldModel const& world, Position3 const& target, ObjectGuid targetGuid,
                                  ObjectGuid member);

    float LossAt(WorldModel const& world, PartyMemberFacts const& member, PositionField const& field,
                 std::vector<Position3> const& points, Position3 const& p);
}
}

#endif
