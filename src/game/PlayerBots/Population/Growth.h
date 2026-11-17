/*
 * World population bots: how far a roster member got, from the owner's progress (the anchor).
 * Pure functions, no state. Design: .kiro/docs/2026-11-17-fixed-roster.md (workspace root).
 */

#ifndef MANGOS_POPULATION_GROWTH_H
#define MANGOS_POPULATION_GROWTH_H

#include "Common.h"
#include "Population/PopulationTypes.h"

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

        // Gear limits at this level. Below 60 the item level follows the bot's level and most
        // pieces are grey / white; at 60 nobody goes above the owner's team (gearCap = best
        // average item level the owner's group wore, 0 = unknown). Low levels leave jewelry empty.
        GearPlan Gear(RosterMember const& member, uint8 level, uint32 gearCap);

        // Copper in the bags: little, mostly decided by wealth.
        uint32 Money(RosterMember const& member, uint8 level);
    }
}

#endif
