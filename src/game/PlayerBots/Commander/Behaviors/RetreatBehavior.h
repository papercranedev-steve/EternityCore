/*
 * PartyBot Commander layer 3: executes TeamPlan::retreat (`.partybot ai retreat`).
 * Rules: combat scenario 11, T+20s .. T+40s (.kiro/docs/combat-scenarios/2026-11-01-11-open-world-elite.md).
 *
 * Every bot, every role, every tick while the retreat runs: one Override-tier Retreat action.
 *  - Override hides all other modules (taunts, interrupts, focus, pull): any hit or stun on an
 *    enemy extends its leash for the whole assisting group.
 *  - The action drops the target, stops auto shot and calls the pet back, then runs to the point.
 *  - Because the arbiter acts every tick, the original rotation never runs: no roots, stuns,
 *    taunts or heals. First version: healers do not heal while running either.
 *  - At the point, a bot an enemy is chasing keeps moving across the point: a chaser that
 *    reaches a target standing still for 5 s extends its leash (TargetedMovementGenerator ~390).
 */

#ifndef MANGOS_COMMANDER_RETREAT_BEHAVIOR_H
#define MANGOS_COMMANDER_RETREAT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class RetreatBehavior : public IBehavior
    {
    public:
        // Paths are longer than the straight line; the hold is renewed when it runs out anyway.
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 1000;
        // At the point: a chaser this close makes the bot step to the other side of the point.
        static constexpr float CHASER_DISTANCE = 8.0f;
        // Stays inside RetreatAssigner::ARRIVED_DISTANCE and inside RetreatPlanner::SAFETY_MARGIN.
        static constexpr float SIDESTEP_DISTANCE = 7.0f;

        char const* GetId() const override { return "retreat"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
