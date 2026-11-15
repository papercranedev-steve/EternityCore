/*
 * PartyBot Commander layer 2 (brain), doctrine step: enemies an off-tank holds for the party
 * besides the main tank (user 2026-11-15: the hunter's pet first, a special off-tank with no
 * taunt, no survival spells, no area threat and less health; later a second tank). Writes
 * DoctrinePlan::offTanks and stamps their enemies STAMP_OFF_TANK. Under the Area tactic too
 * (user: a warrior holds an area pile badly, one enemy less on him helps).
 *
 * Holders: members with PartyMemberFacts::canOffTank that can act, only while there is a
 * main tank (without one a pet with its taunt on autocast holds everything it is hit by,
 * ThreatBehavior). One enemy per holder (no area threat to keep more).
 *
 * Kept (from the last refresh): while the enemy is alive, acting and not controlled, the holder
 * can act, is not in danger (SurvivalValue::Of: it gives the enemy up, the main tank's rescue
 * takes it, "other roles may fail" works both ways) and the enemy hit it within LOST_MS.
 *
 * Taken: an enemy hitting a free holder (it won the threat: the brain's rescue gave it the
 * enemy, RescueAssigner, or it got it on its own), when the holder has at least ADOPT_MIN_PCT
 * health and is not in danger, and the enemy is not one the party must kill first or need not
 * hold (KillNow, KillOrder, FreeAlly, SelfDestruct, Swarm, NoHold, HoldFire), a player's target,
 * the heavy enemy of a pile, or the main tank's own (its victim, the pull target), unless the
 * rescue gave it to the holder.
 * Pure (facts and plan; plan.rescues: the last refresh's).
 */

#ifndef MANGOS_COMMANDER_OFF_TANK_H
#define MANGOS_COMMANDER_OFF_TANK_H

#include "Commander/Coordination/TeamPlan.h"

namespace Commander
{
namespace OffTank
{
    // Untested guesses.
    uint32 const LOST_MS = 3000;
    float const ADOPT_MIN_PCT = 60.0f;

    // `m` can hold an enemy as an off-tank now (header "Holders").
    bool IsHolder(WorldModel const& world, PartyMemberFacts const& m);
    // `m` holds nothing yet and is fit to take an enemy (header "Taken": health, danger).
    bool IsFree(WorldModel const& world, DoctrinePlan const& d, PartyMemberFacts const& m);
    // An off-tank may hold `e` (header "Taken": its stamps, not a pile's heavy enemy). The main
    // tank's own enemy is the caller's check.
    bool MayTake(DoctrinePlan const& d, EnemyFacts const& e);

    void Apply(WorldModel const& world, TeamPlan const& plan, DoctrinePlan const& prev, DoctrinePlan& d);
}
}

#endif
