/*
 * PartyBot Commander layer 1 (eyes): candidate spots from the position field
 * (PositionField.h, doc .kiro/docs/2026-11-10-position-field.md section 7, step B).
 *
 * Collect: the field cells in a band around a point that a member may stand on:
 *  - standable (ground, linked to the participants);
 *  - no idle creature covers the cell (the end of a walk is checked strictly, like
 *    SafeRoute: a circle the member stands in only lets the walk out, not the stop);
 *  - with `patrolBeforeSec` > 0: no patrol reaches the cell before then (pass the fight's
 *    remaining time, Value::FightRemainingSec);
 *  - when the member's own cell is standable: reachable by a grid walk around idle creatures
 *    (walkYd). When it is not (on a crate, a cell not flooded yet), or the grid walk reaches
 *    none of the band (a member far off, the field between not covered), walkYd is -1 and
 *    every cell that passes the rest is returned: the caller's path check decides.
 * Facts only; the caller filters for its purpose, sorts and runs its live checks (line of
 * sight, SafeRoute::PathIsSafe) on the best few.
 */

#ifndef MANGOS_COMMANDER_FIELD_SPOTS_H
#define MANGOS_COMMANDER_FIELD_SPOTS_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    class PositionField;

    namespace FieldSpots
    {
        struct Spot
        {
            Position3 pos;              // cell centre at its ground height
            float straightYd = 0.0f;    // 2D, from the member
            float walkYd = -1.0f;       // grid walk from the member, -1 = not known
            float patrolSec = -1.0f;    // FieldCell::patrolSec

            // Walk estimate in yards: the grid walk, else the straight line times `pathFactor`.
            float WalkEstimate(float pathFactor) const { return walkYd >= 0.0f ? walkYd : straightYd * pathFactor; }
        };

        struct Query
        {
            Position3 center;
            float minYd = 0.0f;         // 2D distance band around `center`
            float maxYd = 0.0f;
            float patrolBeforeSec = 0.0f;
        };

        std::vector<Spot> Collect(PositionField const& field, PartyMemberFacts const& member, Query const& q);
    }
}

#endif
