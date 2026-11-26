/*
 * PartyBot Commander layer 1 (eyes): does a party member have to move right now? What a root
 * or a slow on it costs depends on it (DispelValue: a dispel, Blessing of Freedom; user
 * 2026-11-26: "a root only matters when it keeps the member from moving where it has to").
 * Pure function of the WorldModel. The first reason that applies:
 *   Melee      a healer or ranged member with a melee enemy on it (walks away: EscapeBehavior)
 *   Hazard     it stands in a zone dangerous to its role (HazardZones)
 *   Reach      a melee member (tank included) whose victim is beyond melee reach; a tank with
 *              an enemy hitting another member beyond its reach (it has to go and take it)
 *   HealReach  a bot that heals the team with a member taking damage beyond heal range
 * Moves the plan orders (retreat) are the caller's: perception does not read the plan.
 */

#ifndef MANGOS_COMMANDER_MOVE_NEED_H
#define MANGOS_COMMANDER_MOVE_NEED_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

    enum class MoveReason : uint8
    {
        None,
        Melee,
        Hazard,
        Reach,
        HealReach,
    };

    namespace MoveNeed
    {
        // Melee reach past the enemy's edge (ATTACK_DISTANCE).
        static constexpr float MELEE_REACH_YD = 5.0f;
        static constexpr float HEAL_RANGE_YD = 40.0f;

        MoveReason Of(WorldModel const& world, PartyMemberFacts const& member);
        char const* Name(MoveReason reason);
    }
}

#endif
