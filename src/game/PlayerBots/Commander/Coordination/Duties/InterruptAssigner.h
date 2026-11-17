/*
 * PartyBot Commander layer 2: who interrupts which enemy (step 4).
 * Design: .kiro/docs/2026-11-01-interrupt-assignment.md
 *
 * Duties module (2026-11-03): one need per enemy worth an interrupter, one candidate per usable
 * ability of each bot, ranked by the SCORE_* below. DutyAllocator picks the primary and the
 * backup (DutyAllocator.h), so a bot primary for another duty kind is not primary here.
 *
 * The doctrine's stamps first (2026-11-13): an ability whose control ends on the next hit
 * (InterruptAbility::breaksOnDamage, Gouge) is not offered on an enemy stamped NoMez (the
 * area pile, fodder, cleanup), the same rule as CrowdControlAssigner; logged as rejected.
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
        // The same in points, on the candidate's value (benefit - cost; DutyAllocator weighs
        // whole allocations by it, the SCORE_* only order the candidates of one enemy).
        static constexpr float STICKY_POINTS = 0.5f;
        static constexpr float SCORE_TANK = -0.5f;          // tanks prefer holding threat
        static constexpr float SCORE_NOT_READY = -3.0f;     // only for enemies not casting right now
        // Ranged ability without line of sight to the live caster (EnemyFacts::outOfSight):
        // never primary, last choice as backup.
        static constexpr float SCORE_NO_SIGHT = -5.0f;
        // The bot keeps failing to cast this ability on this enemy (PartyMemberFacts::
        // IsFailingCast): never primary, last choice as backup, until a cast of it goes out or
        // the failures are BotDirectives::CAST_FAILURE_MEMORY_MS old.
        static constexpr float SCORE_FAILING = -5.0f;
        // Live cast out of range: primary only for a bot that walks in (a melee ability of a
        // bot that is not the tank, InterruptBehavior "go to caster") and only if walking in
        // plus this reaction time ends before the cast does (it can still be backup).
        static constexpr float REACTION_SEC = 0.5f;

        char const* GetId() const override { return "interrupt"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
