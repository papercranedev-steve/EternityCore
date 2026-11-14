/*
 * PartyBot Commander layer 1 (eyes): candidate spots from the position field
 * (PositionField.h, doc .kiro/docs/2026-11-10-position-field.md section 7, step B).
 *
 * Collect: the field cells in a band around a point that a member may stand on:
 *  - standable (ground, linked to the participants);
 *  - no idle creature covers the cell (the end of a walk is checked strictly, like
 *    SafeRoute: a circle the member stands in only lets the walk out, not the stop);
 *    Query::tight: on the hard line (FieldCell::idleTight);
 *  - with `patrolBeforeSec` > 0: no patrol reaches the cell before then (how long we stay:
 *    RouteGuard::StaySec, the fight's remaining time once it is on);
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

            // Another member (not a pet) stands there or walks there (PartyMemberFacts::standAt
            // within TAKEN_YD of the cell centre).
            bool taken = false;

            // Walk estimate in yards: the grid walk, else the straight line times `pathFactor`.
            float WalkEstimate(float pathFactor) const { return walkYd >= 0.0f ? walkYd : straightYd * pathFactor; }
        };

        float const TAKEN_YD = 1.5f;
        // Two spots whose order keys differ by at most this count as equally good (one cell of walk).
        float const TIE = 2.0f;

        struct Query
        {
            Position3 center;
            float minYd = 0.0f;         // 2D distance band around `center`
            float maxYd = 0.0f;
            float patrolBeforeSec = 0.0f;
            // The hard line (FieldCell::idleTight, SafeRoute::TIGHT_MARGIN) for the cell and the
            // grid walk: a member who finds nothing otherwise and cannot do its job where it is.
            bool tight = false;
        };

        // From world.field (must be set).
        std::vector<Spot> Collect(WorldModel const& world, PartyMemberFacts const& member, Query const& q);

        // `spots` sorted best first by `key` (lower is better). A spot nobody holds goes before
        // a taken one whose key it is within `tie` of: equally good places are shared out, a
        // clearly better one is still taken by two (standing together costs nothing; mechanics
        // that punish it are hazard facts, position field doc section 6). `tie`: TIE for keys in
        // yards of walk; keys in points pass the points of one cell of walk.
        template <class T, class Key, class Taken>
        void PreferFree(std::vector<T>& spots, Key key, Taken taken, float tie = TIE)
        {
            for (size_t i = 0; i < spots.size(); ++i)
            {
                if (!taken(spots[i]))
                    continue;
                float const limit = key(spots[i]) + tie;
                for (size_t j = i + 1; j < spots.size() && key(spots[j]) <= limit; ++j)
                {
                    if (taken(spots[j]))
                        continue;
                    T moved = spots[j];
                    spots.erase(spots.begin() + j);
                    spots.insert(spots.begin() + i, moved);
                    break;
                }
            }
        }
    }
}

#endif
