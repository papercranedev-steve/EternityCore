/*
 * World population bots: how far a roster member got, from the owner's progress (the anchor).
 * Pure functions, no state. Design: .kiro/docs/2026-11-17-fixed-roster.md (workspace root).
 */

#ifndef MANGOS_POPULATION_GROWTH_H
#define MANGOS_POPULATION_GROWTH_H

#include "Common.h"

namespace Population
{
    struct RosterMember;

    namespace Growth
    {
        // Level of the member while the owner's highest level is anchorLevel.
        // Never more than anchorLevel + leadCap (and 60).
        uint8 Level(RosterMember const& member, uint8 anchorLevel, uint8 leadCap);

        // The member stopped playing once the owner passed its quit level.
        bool IsRetired(RosterMember const& member, uint8 anchorLevel);
    }
}

#endif
