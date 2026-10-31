/*
 * PartyBot Commander layer 2: who interrupts which enemy (step 4).
 * Design: .kiro/docs/2026-11-01-interrupt-assignment.md
 */

#ifndef MANGOS_COMMANDER_INTERRUPT_ASSIGNER_H
#define MANGOS_COMMANDER_INTERRUPT_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class InterruptAssigner : public IAssigner
    {
    public:
        static constexpr float RANGE_SLACK = 2.0f;          // yards added to spell range for "in range"
        static constexpr float SCORE_IN_RANGE = 2.0f;
        static constexpr float SCORE_PER_YARD_OUT = 0.1f;   // penalty per yard outside range
        static constexpr float SCORE_PER_COOLDOWN_SEC = 0.05f;
        static constexpr float SCORE_ALREADY_ON_TARGET = 1.0f;
        static constexpr float SCORE_STICKY = 1.5f;         // keep last assignment
        static constexpr float SCORE_TANK = -0.5f;          // tanks prefer holding threat
        static constexpr float SCORE_NOT_READY = -3.0f;     // only for enemies not casting right now

        char const* GetId() const override { return "interrupt"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
